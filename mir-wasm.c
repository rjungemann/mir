/* This file is a part of MIR project.
   Copyright (C) 2020-2024 Vladimir Makarov <vmakarov.gcc@gmail.com>.

   wasm32 / Emscripten "target" for MIR.

   WebAssembly cannot generate machine code at runtime, so the JIT
   generator is unavailable and MIR can only run in interpreter mode.
   Everything here exists to satisfy the interpreter, and the one piece
   that carries real weight is _MIR_get_ff_call: mir-interp.c routes
   every call from interpreted code into a native function through it
   (see get_ff_interface).  A stubbed-out version means printf and all
   of libc silently do nothing.

   The implementation trampolines through JavaScript.  A wasm function
   pointer is an index into the wasm table, and wasmTable.get(ptr) yields
   a JS-callable function; JS can then spread an argument list of
   arbitrary length and type.  That sidesteps the fact that a wasm
   call_indirect is strictly type-checked and would otherwise require one
   pre-generated C stub per signature (~4^nargs of them).

   Build requirements:
     -sWASM_BIGINT                              (i64 <-> BigInt)
     -sEXPORTED_RUNTIME_METHODS=wasmTable,UTF8ToString
*/

#include "mir.h"

#include <emscripten.h>
#include <stdint.h>
#include <string.h>

#define MIR_DIRECT_DISPATCH 0
#define MAX_HARD_REG 0

static const char *const target_hard_reg_names[] = {NULL};

static inline int target_hard_reg_type_ok_p (MIR_reg_t hard_reg MIR_UNUSED,
                                             MIR_type_t type MIR_UNUSED) {
  return 0;
}

static inline int target_fixed_hard_reg_p (MIR_reg_t hard_reg MIR_UNUSED) { return 0; }

static inline int target_locs_num (MIR_reg_t loc MIR_UNUSED, MIR_type_t type MIR_UNUSED) {
  return 1;
}

/* ---------------------------------------------------------------- */
/* Foreign function calls                                            */
/* ---------------------------------------------------------------- */

#define WASM_FF_MAX_SIGS 100 /* interned one per distinct signature */
#define WASM_FF_MAX_ARGS 32
#define WASM_FF_VA_BUF_SIZE 512

/* Type codes handed to the JS side. */
#define WASM_TC_I32 'i'
#define WASM_TC_I64 'j'
#define WASM_TC_F32 'f'
#define WASM_TC_F64 'd'
#define WASM_TC_VOID 'v'

typedef struct {
  int used;
  int vararg_p;
  size_t nres;
  size_t nargs;   /* total, including the variadic tail */
  size_t nfixed;  /* number of declared (non-variadic) parameters */
  char res_code;  /* WASM_TC_*, or WASM_TC_VOID */
  MIR_type_t arg_types[WASM_FF_MAX_ARGS];
} wasm_ff_sig_t;

static wasm_ff_sig_t wasm_ff_sigs[WASM_FF_MAX_SIGS];
static int wasm_ff_sigs_num = 0;

/* Map a MIR type onto the wasm value type it is passed as.  wasm32 is
   ILP32, so pointers are i32. */
static char wasm_type_code (MIR_type_t t) {
  switch (t) {
  case MIR_T_I8:
  case MIR_T_U8:
  case MIR_T_I16:
  case MIR_T_U16:
  case MIR_T_I32:
  case MIR_T_U32:
  case MIR_T_P: return WASM_TC_I32;
  case MIR_T_I64:
  case MIR_T_U64: return WASM_TC_I64;
  case MIR_T_F: return WASM_TC_F32;
  case MIR_T_D:
  case MIR_T_LD: return WASM_TC_F64; /* long double is double on this target */
  default: return WASM_TC_I32;       /* blocks are passed by address */
  }
}

/* Call wasmTable[fp] with nargs arguments read out of SLOTS (one 8-byte
   slot each, tagged by SIG), storing any result into RETBUF. */
EM_JS (void, mir_wasm_js_call,
       (uint32_t fp, const char *sig, char res_code, uint8_t *slots, uint8_t *retbuf), {
         var s = UTF8ToString (sig);
         var f = wasmTable.get (fp);
         if (!f) throw new Error ("MIR: bad function pointer " + fp);
         var args = [];
         for (var i = 0; i < s.length; i++) {
           var off = slots + i * 8;
           switch (s.charCodeAt (i)) {
           case 105: args.push (HEAP32[off >> 2]); break;               /* 'i' */
           case 106: args.push (HEAP64[off >> 3]); break;               /* 'j' */
           case 102: args.push (HEAPF32[off >> 2]); break;              /* 'f' */
           case 100: args.push (HEAPF64[off >> 3]); break;              /* 'd' */
           default: throw new Error ("MIR: bad type code " + s[i]);
           }
         }
         var r = f.apply (null, args);
         switch (res_code) {
         case 105: HEAP32[retbuf >> 2] = r; break;
         case 106: HEAP64[retbuf >> 3] = r; break;
         case 102: HEAPF32[retbuf >> 2] = r; break;
         case 100: HEAPF64[retbuf >> 3] = r; break;
         default: break; /* void */
         }
       });

static void wasm_ff_dispatch (int sig_index, void *addr, void *res_and_args) {
  wasm_ff_sig_t *sig = &wasm_ff_sigs[sig_index];
  MIR_val_t *ras = (MIR_val_t *) res_and_args;
  MIR_val_t *args = ras + sig->nres;
  uint8_t slots[WASM_FF_MAX_ARGS * 8];
  char codes[WASM_FF_MAX_ARGS + 1];
  uint8_t va_buf[WASM_FF_VA_BUF_SIZE];
  uint8_t retbuf[8];
  size_t i, ncall = 0, va_off = 0;

  memset (slots, 0, sizeof (slots));
  memset (retbuf, 0, sizeof (retbuf));

  for (i = 0; i < sig->nargs && ncall < WASM_FF_MAX_ARGS; i++) {
    MIR_type_t t = sig->arg_types[i];
    uint8_t *slot;

    if (sig->vararg_p && i >= sig->nfixed) {
      /* Variadic tail goes into a buffer, not into registers.  Emscripten
         lowers f(fixed..., ...) to f(fixed..., void *va_buf).

         NOTE: mir-interp.c widens every variadic integer argument to
         MIR_T_I64 before we see it, so the original C width is lost here.
         We lay integers out as 4-byte int/pointer slots, which is what
         %d/%c/%s/%p need.  A genuine `long long` (%lld) would need 8 and
         is therefore not supported yet -- fixing it properly means having
         the c2mir wasm32 target build the buffer, where the real C types
         are still known. */
      if (wasm_type_code (t) == WASM_TC_F64 || wasm_type_code (t) == WASM_TC_F32) {
        double d = (t == MIR_T_F) ? (double) args[i].f : args[i].d;
        va_off = (va_off + 7) & ~(size_t) 7;
        if (va_off + 8 > sizeof (va_buf)) break;
        memcpy (va_buf + va_off, &d, 8);
        va_off += 8;
      } else {
        uint32_t v = (uint32_t) args[i].u;
        va_off = (va_off + 3) & ~(size_t) 3;
        if (va_off + 4 > sizeof (va_buf)) break;
        memcpy (va_buf + va_off, &v, 4);
        va_off += 4;
      }
      continue;
    }

    slot = slots + ncall * 8;
    codes[ncall] = wasm_type_code (t);
    switch (codes[ncall]) {
    case WASM_TC_I32: {
      uint32_t v = (uint32_t) args[i].u;
      memcpy (slot, &v, 4);
      break;
    }
    case WASM_TC_I64: {
      uint64_t v = args[i].u;
      memcpy (slot, &v, 8);
      break;
    }
    case WASM_TC_F32: {
      float v = args[i].f;
      memcpy (slot, &v, 4);
      break;
    }
    default: {
      double v = (t == MIR_T_LD) ? (double) args[i].ld : args[i].d;
      memcpy (slot, &v, 8);
      break;
    }
    }
    ncall++;
  }

  /* The variadic buffer is passed as one extra pointer argument. */
  if (sig->vararg_p && ncall < WASM_FF_MAX_ARGS) {
    uint32_t p = (uint32_t) (uintptr_t) va_buf;
    codes[ncall] = WASM_TC_I32;
    memcpy (slots + ncall * 8, &p, 4);
    ncall++;
  }
  codes[ncall] = '\0';

  mir_wasm_js_call ((uint32_t) (uintptr_t) addr, codes, sig->res_code, slots, retbuf);

  if (sig->nres > 0) {
    switch (sig->res_code) {
    case WASM_TC_I32: {
      int32_t v;
      memcpy (&v, retbuf, 4);
      ras[0].i = v;
      break;
    }
    case WASM_TC_I64: {
      int64_t v;
      memcpy (&v, retbuf, 8);
      ras[0].i = v;
      break;
    }
    case WASM_TC_F32: {
      float v;
      memcpy (&v, retbuf, 4);
      ras[0].f = v;
      break;
    }
    case WASM_TC_F64: {
      double v;
      memcpy (&v, retbuf, 8);
      ras[0].d = v;
      break;
    }
    default: break;
    }
  }
}

/* _MIR_get_ff_call must return a plain void (*) (void *, void *), so the
   signature has to be baked into the pointer itself.  Without codegen the
   only way is a pool of pre-generated stubs, each closing over its index. */
#define WASM_FF_STUB(R, C)                                            \
  static void wasm_ff_stub_##R##_##C (void *addr, void *res_and_args) { \
    wasm_ff_dispatch ((R) *10 + (C), addr, res_and_args);             \
  }
#define WASM_FF_STUB_ROW(R)                                                     \
  WASM_FF_STUB (R, 0) WASM_FF_STUB (R, 1) WASM_FF_STUB (R, 2) WASM_FF_STUB (R, 3) \
    WASM_FF_STUB (R, 4) WASM_FF_STUB (R, 5) WASM_FF_STUB (R, 6)                 \
      WASM_FF_STUB (R, 7) WASM_FF_STUB (R, 8) WASM_FF_STUB (R, 9)

WASM_FF_STUB_ROW (0)
WASM_FF_STUB_ROW (1)
WASM_FF_STUB_ROW (2)
WASM_FF_STUB_ROW (3)
WASM_FF_STUB_ROW (4)
WASM_FF_STUB_ROW (5)
WASM_FF_STUB_ROW (6)
WASM_FF_STUB_ROW (7)
WASM_FF_STUB_ROW (8)
WASM_FF_STUB_ROW (9)

#define WASM_FF_STUB_REF(R, C) wasm_ff_stub_##R##_##C,
#define WASM_FF_STUB_REF_ROW(R)                                                             \
  WASM_FF_STUB_REF (R, 0) WASM_FF_STUB_REF (R, 1) WASM_FF_STUB_REF (R, 2)                   \
    WASM_FF_STUB_REF (R, 3) WASM_FF_STUB_REF (R, 4) WASM_FF_STUB_REF (R, 5)                 \
      WASM_FF_STUB_REF (R, 6) WASM_FF_STUB_REF (R, 7) WASM_FF_STUB_REF (R, 8)               \
        WASM_FF_STUB_REF (R, 9)

static void (*const wasm_ff_stubs[WASM_FF_MAX_SIGS]) (void *, void *) = {
  WASM_FF_STUB_REF_ROW (0) WASM_FF_STUB_REF_ROW (1) WASM_FF_STUB_REF_ROW (2)
    WASM_FF_STUB_REF_ROW (3) WASM_FF_STUB_REF_ROW (4) WASM_FF_STUB_REF_ROW (5)
      WASM_FF_STUB_REF_ROW (6) WASM_FF_STUB_REF_ROW (7) WASM_FF_STUB_REF_ROW (8)
        WASM_FF_STUB_REF_ROW (9)};

void *_MIR_get_ff_call (MIR_context_t ctx, size_t nres, MIR_type_t *res_types, size_t nargs,
                        _MIR_arg_desc_t *arg_descs, size_t arg_vars_num) {
  wasm_ff_sig_t *sig;
  size_t i;

  if (nres > 1)
    MIR_get_error_func (ctx) (MIR_call_op_error,
                              "wasm32: multiple return values are not supported");
  if (nargs > WASM_FF_MAX_ARGS)
    MIR_get_error_func (ctx) (MIR_call_op_error, "wasm32: too many arguments in a foreign call");
  if (wasm_ff_sigs_num >= WASM_FF_MAX_SIGS)
    MIR_get_error_func (ctx) (MIR_call_op_error,
                              "wasm32: too many distinct foreign call signatures");

  sig = &wasm_ff_sigs[wasm_ff_sigs_num];
  sig->used = 1;
  sig->nres = nres;
  sig->nargs = nargs;
  sig->nfixed = arg_vars_num;
  sig->vararg_p = nargs > arg_vars_num;
  sig->res_code = nres == 0 ? WASM_TC_VOID : wasm_type_code (res_types[0]);
  for (i = 0; i < nargs; i++) sig->arg_types[i] = arg_descs[i].type;

  return (void *) wasm_ff_stubs[wasm_ff_sigs_num++];
}

/* ---------------------------------------------------------------- */
/* Remaining interpreter hooks                                       */
/* ---------------------------------------------------------------- */

/* These concern native code calling *back into* interpreted functions and
   runtime-generated thunks.  Implementing them needs Emscripten's
   addFunction (which appends to the wasm table at runtime); until then
   they stay inert.  Interpreted code calling out (the REPL case) works. */

static void *bstart_func (void) {
  static char buf[16];
  return buf;
}
static void bend_func (void *p MIR_UNUSED) {}
void *_MIR_get_bstart_builtin (MIR_context_t ctx MIR_UNUSED) { return (void *) bstart_func; }
void *_MIR_get_bend_builtin (MIR_context_t ctx MIR_UNUSED) { return (void *) bend_func; }

void *va_arg_builtin (void *p, uint64_t t) {
  /* va_list is a pointer into the buffer built by wasm_ff_dispatch. */
  void **va = (void **) p;
  void *a = *va;

  switch ((MIR_type_t) t) {
  case MIR_T_I64:
  case MIR_T_U64:
  case MIR_T_D:
  case MIR_T_LD:
    a = (void *) (((uintptr_t) a + 7) & ~(uintptr_t) 7);
    *va = (char *) a + 8;
    break;
  default:
    a = (void *) (((uintptr_t) a + 3) & ~(uintptr_t) 3);
    *va = (char *) a + 4;
    break;
  }
  return a;
}

void va_block_arg_builtin (void *res, void *p, size_t s, uint64_t ncase MIR_UNUSED) {
  void **va = (void **) p;
  if (res != NULL) memcpy (res, *va, s);
  *va = (char *) *va + ((s + 3) & ~(size_t) 3);
}

void va_start_interp_builtin (MIR_context_t ctx MIR_UNUSED, void *p, void *a) {
  void **va = (void **) p;
  *va = a;
}

void va_end_interp_builtin (MIR_context_t ctx MIR_UNUSED, void *p MIR_UNUSED) {}

static void thunk_func (void) {}
void *_MIR_get_thunk (MIR_context_t ctx MIR_UNUSED) { return (void *) thunk_func; }
void *_MIR_get_thunk_addr (MIR_context_t ctx MIR_UNUSED, void *thunk MIR_UNUSED) {
  return (void *) thunk_func;
}
void _MIR_redirect_thunk (MIR_context_t ctx MIR_UNUSED, void *thunk MIR_UNUSED,
                          void *to MIR_UNUSED) {}

static void interp_shim_func (void *func MIR_UNUSED, void *args MIR_UNUSED) {}
void *_MIR_get_interp_shim (MIR_context_t ctx MIR_UNUSED, MIR_item_t func_item MIR_UNUSED,
                            void *handler MIR_UNUSED) {
  return (void *) interp_shim_func;
}

static void wrapper_func (void) {}
void *_MIR_get_wrapper (MIR_context_t ctx MIR_UNUSED, MIR_item_t called_func MIR_UNUSED,
                        void *hook_address MIR_UNUSED) {
  return (void *) wrapper_func;
}
static char wrapper_end_buf[16];
void *_MIR_get_wrapper_end (MIR_context_t ctx MIR_UNUSED) { return wrapper_end_buf; }

/* Code generator entry points referenced by c2mir-driver.c.  There is no
   generator on this target; interpreter mode only. */
void MIR_gen_set_debug_file (MIR_context_t ctx MIR_UNUSED, const char *file MIR_UNUSED) {}
void MIR_gen_set_debug_level (MIR_context_t ctx MIR_UNUSED, int level MIR_UNUSED) {}
void MIR_set_gen_interface (MIR_context_t ctx MIR_UNUSED, void *gen_interface MIR_UNUSED) {}
void MIR_set_lazy_bb_gen_interface (MIR_context_t ctx MIR_UNUSED, void *i MIR_UNUSED) {}
void MIR_set_lazy_gen_interface (MIR_context_t ctx MIR_UNUSED, void *i MIR_UNUSED) {}
void MIR_gen_finish (MIR_context_t ctx MIR_UNUSED) {}
