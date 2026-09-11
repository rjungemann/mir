#!/bin/sh
# Build and run the wasm32 FFI test under node.
# Requires the Emscripten SDK (emcc) and node on PATH.
set -e
cd "$(dirname "$0")/.."
emcc -O0 -I. -Ic2mir wasm-tests/ffi-test.c mir.c c2mir/c2mir.c \
     -o wasm-tests/ffi-test.js \
     -sWASM_BIGINT \
     -sEXPORTED_RUNTIME_METHODS=wasmTable,UTF8ToString \
     -sTOTAL_MEMORY=256mb -sSTACK_SIZE=8mb
node wasm-tests/ffi-test.js
