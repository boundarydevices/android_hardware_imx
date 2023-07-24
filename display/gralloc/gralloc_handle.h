/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Copyright 2023 NXP.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GRALLOC_HANDLE_H
#define GRALLOC_HANDLE_H

#include <Memory.h>
#include <cutils/native_handle.h>

#include <cstdint>
#include <string>

using namespace fsl;

typedef struct Memory gralloc_handle;
typedef const struct Memory *gralloc_handle_t;

struct gralloc_buffer_descriptor {
    uint32_t width;
    uint32_t height;
    int32_t droid_format;
    uint64_t droid_usage;
    uint32_t drm_format;
    uint64_t use_flags;
    uint64_t reserved_region_size;
    std::string name;
};

#endif
