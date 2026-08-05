/* End-to-end check of the wasm32 MIR target: compile C with c2mir, link it
   in interpreter mode, and call it -- including calls out to libc. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

#include "mir.h"
#include "mir-dlist.h"
#include "c2mir.h"

static const char *src_ptr;

static int string_getc (void *data) {
  (void) data;
  if (*src_ptr == '\0') return EOF;
  return *src_ptr++;
}

static void __attribute__ ((noreturn)) err (MIR_error_type_t t, const char *fmt, ...) {
  (void) t;
  va_list ap;
  va_start (ap, fmt);
  fprintf (stderr, "MIR ERROR: ");
  vfprintf (stderr, fmt, ap);
  fprintf (stderr, "\n");
  va_end (ap);
  exit (1);
}

static int run (const char *code, const char *entry, int64_t *out) {
  MIR_context_t ctx = MIR_init ();
  struct c2mir_options ops;
  MIR_module_t m;
  MIR_item_t f, target = NULL;
  MIR_val_t res;

  MIR_set_error_func (ctx, err);
  c2mir_init (ctx);
  memset (&ops, 0, sizeof (ops));
  ops.message_file = stderr;
  src_ptr = code;

  if (!c2mir_compile (ctx, &ops, string_getc, NULL, "<test>", NULL)) {
    fprintf (stderr, "compile failed\n");
    return 0;
  }
  for (m = DLIST_HEAD (MIR_module_t, *MIR_get_module_list (ctx)); m != NULL;
       m = DLIST_NEXT (MIR_module_t, m)) {
    for (f = DLIST_HEAD (MIR_item_t, m->items); f != NULL; f = DLIST_NEXT (MIR_item_t, f))
      if (f->item_type == MIR_func_item && strcmp (f->u.func->name, entry) == 0) target = f;
    MIR_load_module (ctx, m);
  }
  if (target == NULL) {
    fprintf (stderr, "entry %s not found\n", entry);
    return 0;
  }
  MIR_load_external (ctx, "printf", (void *) printf);
  MIR_load_external (ctx, "puts", (void *) puts);
  MIR_load_external (ctx, "strlen", (void *) strlen);
  MIR_load_external (ctx, "sqrt", (void *) sqrt);
  MIR_load_external (ctx, "abs", (void *) abs);
  MIR_link (ctx, MIR_set_interp_interface, NULL);
  MIR_interp (ctx, target, &res, 0);
  *out = res.i;
  return 1;
}

#define CHECK(label, code, entry, expect)                                       \
  do {                                                                          \
    int64_t r = 0;                                                              \
    printf ("--- %s ---\n", label);                                             \
    if (!run (code, entry, &r)) {                                               \
      printf ("  FAIL (did not run)\n");                                        \
      fails++;                                                                  \
    } else if (r != (expect)) {                                                 \
      printf ("  FAIL got %lld want %lld\n", (long long) r, (long long) (expect)); \
      fails++;                                                                  \
    } else {                                                                    \
      printf ("  ok (returned %lld)\n", (long long) r);                         \
    }                                                                           \
  } while (0)

int main (void) {
  int fails = 0;

  CHECK ("pure arithmetic (no FFI)", "int t (void) { int a = 6, b = 7; return a * b; }", "t", 42);

  CHECK ("printf with no varargs", "#include <stdio.h>\n"
                                   "int t (void) { return printf (\"hello\\n\"); }",
         "t", 6);

  CHECK ("printf %d", "#include <stdio.h>\n"
                      "int t (void) { return printf (\"int=%d\\n\", 42); }",
         "t", 7);

  CHECK ("printf %s and %c", "#include <stdio.h>\n"
                             "int t (void) { return printf (\"%s%c\\n\", \"str\", 33); }",
         "t", 5);

  CHECK ("printf %f (double vararg)", "#include <stdio.h>\n"
                                      "int t (void) { return printf (\"%.2f\\n\", 3.5); }",
         "t", 5);

  /* "7 x 2.5\n" is 8 characters */
  CHECK ("mixed varargs", "#include <stdio.h>\n"
                          "int t (void) { return printf (\"%d %s %.1f\\n\", 7, \"x\", 2.5); }",
         "t", 8);

  /* Known limitation: mir-interp.c widens every variadic integer to I64
     before _MIR_get_ff_call sees it, so the original C width is gone and
     we lay variadic integers out as 4-byte slots.  %lld needs 8. */
  CHECK ("KNOWN-BROKEN printf %lld", "#include <stdio.h>\n"
                                     "int t (void) { return printf (\"%lld\\n\", 12345678901ll); }",
         "t", 12);

  CHECK ("non-variadic ptr->int (strlen)", "#include <string.h>\n"
                                           "int t (void) { return strlen (\"abcde\"); }",
         "t", 5);

  CHECK ("double->double (sqrt)", "#include <math.h>\n"
                                  "int t (void) { return (int) sqrt (144.0); }",
         "t", 12);

  CHECK ("int->int (abs)", "#include <stdlib.h>\n"
                           "int t (void) { return abs (-17); }",
         "t", 17);

  CHECK ("ptr->int (puts)", "#include <stdio.h>\n"
                            "int t (void) { puts (\"via puts\"); return 1; }",
         "t", 1);

  CHECK ("call in a loop", "#include <stdio.h>\n"
                           "int t (void) { int i, n = 0; for (i = 0; i < 3; i++) n += printf (\"i=%d\\n\", i); return n; }",
         "t", 12);

  printf ("\n=== %s ===\n", fails == 0 ? "ALL PASSED" : "FAILURES PRESENT");
  return fails != 0;
}
