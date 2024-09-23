/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Copyright 2024 NXP
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __GRALLOC_HELPERS_H
#define __GRALLOC_HELPERS_H

#include <system/graphics.h>

#include "gralloc_handle.h"

struct rectangle {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

gralloc_handle_t gralloc_convert_handle(buffer_handle_t handle);
int32_t gralloc_sync_wait(int32_t fence, bool close_fence);

#endif
