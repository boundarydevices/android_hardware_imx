/*
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Copyright 2024 NXP
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#pragma once

#include <stdbool.h>

#include "gralloc_handle.h"

#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define ARRAY_SIZE(A) (sizeof(A) / sizeof(*(A)))
#define PUBLIC __attribute__((visibility("default")))
#define ALIGN(A, B) (((A) + (B)-1) & ~((B)-1))
#define IS_ALIGNED(A, B) (ALIGN((A), (B)) == (A))
#define DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))
#define STRINGIZE_NO_EXPANSION(x) #x
#define STRINGIZE(x) STRINGIZE_NO_EXPANSION(x)

bool drv_pixel_width_height_alignment(gralloc_buffer_descriptor *desc, uint32_t *aligned_width,
                                      uint32_t *aligned_height);
int drv_buffer_info_calculation(gralloc_buffer_descriptor *desc, uint32_t aligned_width,
                                uint32_t aligned_height);
