/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Copyright 2024 NXP
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#pragma once

#include <aidl/android/hardware/graphics/common/BufferUsage.h>
#include <aidl/android/hardware/graphics/common/PixelFormat.h>
#include <aidl/android/hardware/graphics/common/PlaneLayout.h>
#include <android/hardware/graphics/mapper/4.0/IMapper.h>
#include <drm_fourcc.h>

#include <string>
#include <vector>

#include "gralloc_handle.h"

using aidl::android::hardware::graphics::common::BufferUsage;
using aidl::android::hardware::graphics::common::PixelFormat;
using aidl::android::hardware::graphics::common::PlaneLayout;
using BufferDescriptorInfoV4 =
        android::hardware::graphics::mapper::V4_0::IMapper::BufferDescriptorInfo;

/* clang-format: off */
struct format_info_t {
    int32_t id; /* Android pixel format */
    uint32_t fourcc;
    uint64_t modifier;
    bool is_rgb; /* RGB format. */
    bool is_yuv; /* YUV format. */
};
/* clang-format: on */

std::string getDrmFormatString(uint32_t drmFormat);
std::string getPixelFormatString(int32_t format);
std::string getUsageString(uint64_t bufferUsage);

const struct format_info_t* getPixleFormatInfo(int32_t pixel_format);

int convertToBufferFlags(uint64_t grallocUsage, uint32_t* outBufferFlags);
int convertToHalDescriptor(const BufferDescriptorInfoV4& descriptor,
                           struct gralloc_buffer_descriptor* outDescriptor);

int getPlaneLayouts(uint32_t drm_format, std::vector<PlaneLayout>* out_layouts);
