/*
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Copyright 2023 NXP
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef HELPERS_H
#define HELPERS_H

#include <stdbool.h>

#include "drv.h"
#include "gralloc_handle.h"

#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define ARRAY_SIZE(A) (sizeof(A) / sizeof(*(A)))
#define PUBLIC __attribute__((visibility("default")))
#define ALIGN(A, B) (((A) + (B)-1) & ~((B)-1))
#define IS_ALIGNED(A, B) (ALIGN((A), (B)) == (A))
#define DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))
#define STRINGIZE_NO_EXPANSION(x) #x
#define STRINGIZE(x) STRINGIZE_NO_EXPANSION(x)

uint32_t drv_height_from_format(uint32_t format, uint32_t height, size_t plane);
uint32_t drv_vertical_subsampling_from_format(uint32_t format, size_t plane);
uint32_t drv_size_from_format(uint32_t format, uint32_t stride, uint32_t height, size_t plane);
int drv_bo_from_format(gralloc_handle *hnd, uint32_t stride, uint32_t aligned_height,
                       uint32_t format);
int drv_bo_from_format_and_padding(gralloc_handle *hnd, uint32_t stride, uint32_t aligned_height,
                                   uint32_t format, uint32_t padding[DRV_MAX_PLANES]);
uint32_t drv_get_standard_fourcc(uint32_t fourcc_internal);
#endif
