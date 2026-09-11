/* Lazy-generation wrapper ABI test.

   _MIR_get_thunk / _MIR_get_wrapper / _MIR_redirect_thunk are what
   MIR_set_lazy_gen_interface is built out of: the first call to a function
   lands in a thunk that jumps to a wrapper, the wrapper calls a generation
   hook, and then it must jump on to the generated code with the ORIGINAL
   call's argument registers intact and the stack still ABI-correct.

   This exercises that path directly, without a generator, so a breakage in
   the hand-written assembly patterns shows up here rather than as a crash
   somewhere inside a JIT.  It is a plain-C test: any target that implements
   the three primitives should pass it.  */

#include <stdio.h>
#include <stdint.h>
#include "../mir.h"

static int hook_calls = 0;

/* ---- integer arguments ------------------------------------------------ */

static int64_t iadd4 (int64_t a, int64_t b, int64_t c, int64_t d) {
  return a + b + c + d;
}

static void *ihook (MIR_context_t ctx, MIR_item_t item) {
  (void) ctx;
  (void) item;
  hook_calls++;
  return (void *) iadd4;
}

/* ---- floating-point arguments ----------------------------------------- */

/* Non-integral values, so a clobbered register and a truncated one are
   distinguishable from a correct result.  */
static double dadd4 (double a, double b, double c, double d) {
  return a + b + c + d;
}

static void *dhook (MIR_context_t ctx, MIR_item_t item) {
  (void) ctx;
  (void) item;
  hook_calls++;
  return (void *) dadd4;
}

static void *make_thunk (MIR_context_t ctx, void *hook) {
  void *thunk = _MIR_get_thunk (ctx);
  _MIR_redirect_thunk (ctx, thunk, _MIR_get_wrapper (ctx, NULL, hook));
  return thunk;
}

int main (void) {
  MIR_context_t ctx = MIR_init ();
  int fail = 0;

  {
    int64_t (*fp) (int64_t, int64_t, int64_t, int64_t)
      = (int64_t (*) (int64_t, int64_t, int64_t, int64_t)) make_thunk (ctx, (void *) ihook);
    int64_t got = fp (1, 20, 300, 4000), want = 4321;
    if (got != want) {
      fprintf (stderr, "integer args: got %lld, want %lld\n", (long long) got,
               (long long) want);
      fail = 1;
    }
  }

  {
    double (*fp) (double, double, double, double)
      = (double (*) (double, double, double, double)) make_thunk (ctx, (void *) dhook);
    double got = fp (7.25, 3.5, 0.125, 1.0625), want = 11.9375;
    if (got != want) {
      fprintf (stderr, "float args: got %f, want %f\n", got, want);
      fail = 1;
    }
  }

  if (hook_calls != 2) {
    fprintf (stderr, "generation hook ran %d times, want 2\n", hook_calls);
    fail = 1;
  }

  MIR_finish (ctx);
  if (!fail) fprintf (stderr, "wrapper ABI: OK\n");
  return fail;
}
