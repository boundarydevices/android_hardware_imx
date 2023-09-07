/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __GRALLOC_HELPERS_H
#define __GRALLOC_HELPERS_H

#include <system/graphics.h>

#include "drv.h"
#include "gralloc_handle.h"

gralloc_handle_t gralloc_convert_handle(buffer_handle_t handle);

int32_t gralloc_sync_wait(int32_t fence, bool close_fence);

uint32_t drv_convert_nxp_format_to_drm_format(int format);
int convert_pixel_format_to_gralloc_format(int32_t format);
int convert_drm_format_to_nxp_format(uint32_t drm_format);
int convert_buffer_usage_to_nxp_usage(int32_t droid_usage);
int convert_bo_use_flages_to_nxp_flags(uint64_t use_flags);

uint32_t drv_resolve_format(uint32_t format, uint64_t use_flags);
#endif
