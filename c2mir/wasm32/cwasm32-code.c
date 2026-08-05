/* This file is a part of MIR project.
   Copyright (C) 2020-2024 Vladimir Makarov <vmakarov.gcc@gmail.com>.
*/

#include "../mirc.h"
#include "mirc_wasm32_emscripten.h"

#include "mirc_wasm32_float.h"
#include "mirc_wasm32_limits.h"
#include "mirc_wasm32_stdarg.h"
#include "mirc_wasm32_stdint.h"
#include "mirc_wasm32_stddef.h"
#include "mirc_wasm32_libc.h"

static string_include_t standard_includes[]
  = {{NULL, mirc}, {NULL, wasm32_mirc}, TARGET_STD_INCLUDES, WASM32_LIBC_INCLUDES};

#define MAX_ALIGNMENT 8

#define ADJUST_VAR_ALIGNMENT(c2m_ctx, align, type) \
  wasm32_adjust_var_alignment (c2m_ctx, align, type)

static int wasm32_adjust_var_alignment (c2m_ctx_t c2m_ctx MIR_UNUSED, int align,
                                        struct type *type MIR_UNUSED) {
  return align;
}

static int invalid_alignment (mir_llong align) {
  return align != 0 && align != 1 && align != 2 && align != 4 && align != 8;
}
