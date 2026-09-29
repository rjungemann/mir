/* A statement expression whose value is a struct/union is copied out into a
   frame slot so sibling statement expressions get independent storage.  That
   slot used to be reserved while the function body was still being checked,
   at the frame size so far -- but the function's stack variables are laid out
   only afterwards, from offset 0, so the slot overlapped the first of them.
   The copy-out then overwrote that variable: typically a by-value struct
   parameter, as in k() below, or a sibling argument of the same call, as in
   run() below.  */
#include <stdint.h>

typedef struct { int64_t a, b; } T; /* 16 bytes */
typedef struct { int64_t x, y, z; } S; /* 24 bytes */

static int64_t use (T f) { return f.a * 100 + f.b; }

/* Initializer position: `f` was overwritten with {7, 8}.  */
static int64_t k (T f, int64_t p) {
  S s = ({ *(S *) (intptr_t) p; });
  return use (f) + s.x * 0;
}

/* The same with a local and a call inside the statement expression.  */
static int64_t k2 (T f, int64_t p) {
  S s = ({
    int64_t q = p;
    S t = *(S *) (intptr_t) q;
    t.y += (int64_t) use (f) * 0;
    t;
  });
  return use (f) * 1000 + s.y;
}

/* Argument position: the first argument was replaced by the second.  */
static const T NIL = {0, 0};
static int64_t g (T a, T b) { return a.a * 10 + b.a; }
static int64_t run (T acc, int64_t p) {
  return g (acc, ({ int64_t q = p; q ? *(T *) (intptr_t) q : NIL; }));
}

/* Sibling statement expressions still get independent storage (the reason
   the slot exists), in a loop too, and next to locals of nested scopes.  */
static int64_t siblings (int n) {
  int64_t acc = 0;
  for (int i = 0; i < n; i++) {
    S outer = {i, i + 1, i + 2};
    {
      S inner = {100, 200, 300};
      acc += ({ S u = outer; u; }).x - ({ S v = inner; v; }).x + inner.z - outer.z;
    }
  }
  return acc;
}

int main (void) {
  S v = {7, 8, 9};
  T f = {3, 4};
  if (k (f, (int64_t) (intptr_t) &v) != 304) return 1;
  if (k2 (f, (int64_t) (intptr_t) &v) != 304008) return 2;
  T x = {5, 0};
  if (run (f, (int64_t) (intptr_t) &x) != 35) return 3;
  if (run (f, 0) != 30) return 4;
  /* sum over i of (i - 100 + 300 - (i + 2)) = 198 per iteration */
  if (siblings (1000) != 198000) return 5;
  return 0;
}
