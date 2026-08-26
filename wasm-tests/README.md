# wasm32 / Emscripten target

Lets MIR + c2mir be compiled to WebAssembly and run in a browser or under
node. WebAssembly cannot generate machine code at runtime, so the JIT
generator is not built and MIR runs in **interpreter mode only**.

## Building

```sh
emcmake cmake -B build-emcc -S .
cmake --build build-emcc
```

`EMSCRIPTEN` disables `mir-gen.c` and all native executables/tests; a wasm
consumer links `mir_static` into its own Emscripten build.

Consumers must build with:

```
-sWASM_BIGINT
-sEXPORTED_RUNTIME_METHODS=wasmTable,UTF8ToString
```

Both are required by the foreign-call implementation (see below). Omitting
them shows up as `wasmTable is not defined` at the first call out of
interpreted code.

## How foreign calls work

`mir-interp.c` routes every call from interpreted code into a native
function through `_MIR_get_ff_call`, which on native targets JITs a thunk
that marshals arguments into registers. That is impossible here.

A wasm `call_indirect` is strictly type-checked — casting a function
pointer and calling it traps with `null function or function signature
mismatch` unless arity *and* types match exactly — so a table of
pre-generated C stubs would need one entry per signature (~`4^nargs`).

Instead `mir-wasm.c` trampolines through JavaScript. A wasm function
pointer is an index into the wasm table, and `wasmTable.get(ptr)` yields a
JS-callable function; JS can spread an argument list of arbitrary length
and type. `_MIR_get_ff_call` records the signature in a slot and returns
one of 100 pre-generated stubs bound to that slot index (the returned
pointer must be a plain `void (*) (void *, void *)`, so the signature has
to be baked into it). MIR interns one interface per distinct signature, so
100 slots is ample; exhausting the pool raises a MIR error.

Variadic calls use Emscripten's ABI, where `f(fixed..., ...)` lowers to
`f(fixed..., void *vararg_buffer)`. `wasm_ff_dispatch` builds that buffer.

## System headers are the embedder's job

wasm32 ships only compiler-provided headers, exactly like every other
target. It used to also bundle minimal `stdio.h`, `stdlib.h`, `string.h`
and `math.h`, on the grounds that a browser has no filesystem to read the
real ones from. That put the declarations in the compiler and the matching
symbol table in the embedder — two different repositories — and they
drifted, until the headers declared roughly twice as many functions as the
embedder actually supplied. The surplus compiled cleanly and then failed at
`MIR_link`.

Serve libc through `c2mir_options.include_dirs` instead. Emscripten's MEMFS
is readable by c2mir's ordinary `fopen`-based include search, so an embedder
can write headers there at startup and generate both those headers and its
`MIR_load_external` table from a single list, which makes drift impossible.

Note that `standard_includes` is consulted *before* `system_header_dirs`, so
anything the compiler ships under a given name shadows the embedder's copy.

Calling an undeclared function no longer passes silently: c2mir warns that
it is synthesising an `int f()` prototype. That is worth promoting to an
error, because the synthesised prototype takes no arguments and the call is
generated against it, so the arguments are never passed.

## Type model

ILP32: `int`, `long` and pointers are 4 bytes, `long long` is 8. Note this
is the only target where `long` is not 64-bit, so `size_t` and `ptrdiff_t`
are 32-bit too. `long double` is mapped to `double`.

## Testing

```sh
./wasm-tests/run.sh
```

Covers pure interpretation, variadic and non-variadic libc calls, and
calls in a loop.

## Known limitations

- **`%lld` and other 64-bit variadic integers are wrong.** `mir-interp.c`
  widens every variadic integer argument to `MIR_T_I64` before
  `_MIR_get_ff_call` sees it, so the original C width is unrecoverable.
  Variadic integers are laid out as 4-byte slots, which is right for
  `%d`/`%c`/`%s`/`%p` but truncates a genuine `long long`. Fixing this
  properly means having the c2mir wasm32 target build the variadic buffer,
  where the real C types are still known. `wasm-tests/ffi-test.c` has a
  test marked `KNOWN-BROKEN` that fails on exactly this.
- Native code *can* now call back into interpreted functions: when an
  interpreted function's address is about to be handed to a native callee,
  `wasm_ff_dispatch` swaps in a real wasm table entry created with
  Emscripten's `addFunction`. `qsort` and `bsearch` with an interpreted
  comparator work, including a comparator that itself calls interpreted or
  native code. Requires `-sALLOW_TABLE_GROWTH` and the `addFunction`,
  `stackAlloc`, `stackSave` and `stackRestore` runtime methods.

  Two things to know if you touch that code. `sizeof (MIR_val_t)` is **16**
  here, not 8 — the union carries a host `long double` and Emscripten's is
  128-bit — so the marshalling buffer's stride has to come from `sizeof`
  rather than being assumed. And wasm32 is ILP32, so a function pointer and
  an `int` are the same wasm type; the substitution therefore triggers on any
  i32 argument that happens to be a live, slot-aligned interpreted thunk
  address, which is vanishingly unlikely but not impossible.
- **Calling an interpreted variadic function from interpreted code** raises
  an explicit error rather than returning garbage.
- `long double` precision is that of `double`, so `%Lf` is not usable.
- Multiple return values are rejected.
