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

## Built-in system headers

Unlike every other target, wasm32 ships minimal `stdio.h`, `stdlib.h`,
`string.h` and `math.h` (`c2mir/wasm32/mirc_wasm32_libc.h`). A browser has
no filesystem to read the real ones from, and without a prototype `printf`
would be implicitly declared — producing a no-argument MIR proto that
passes garbage. Symbols still have to be supplied via `MIR_load_external`.

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
- **Native code cannot call back into interpreted functions.**
  `_MIR_get_interp_shim`, `_MIR_get_wrapper` and `_MIR_get_thunk` are
  inert. Implementing them needs Emscripten's `addFunction` to append to
  the wasm table at runtime. This means e.g. passing an interpreted
  comparator to `qsort` will not work.
- `long double` precision is that of `double`, so `%Lf` is not usable.
- Multiple return values are rejected.
