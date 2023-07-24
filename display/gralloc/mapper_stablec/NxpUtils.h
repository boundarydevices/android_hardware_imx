/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Copyright 2023 NXP
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <aidl/android/hardware/graphics/common/PlaneLayout.h>
#include <android/hardware/graphics/common/1.2/types.h>
#include <android/hardware/graphics/mapper/4.0/IMapper.h>

#include <string>
#include <vector>

#include "gralloc_handle.h"

std::string getDrmFormatString(uint32_t drmFormat);

std::string getPixelFormatString(android::hardware::graphics::common::V1_2::PixelFormat format);

int convertToDrmFormat(android::hardware::graphics::common::V1_2::PixelFormat format,
                       uint32_t* outDrmFormat);

int convertToBufferUsage(uint64_t grallocUsage, uint64_t* outBufferUsage);

int convertToMemDescriptor(
        const android::hardware::graphics::mapper::V4_0::IMapper::BufferDescriptorInfo& descriptor,
        struct gralloc_buffer_descriptor* outMemDescriptor);

int convertToMapUsage(uint64_t grallocUsage, uint32_t* outMapUsage);

int convertToFenceFd(const android::hardware::hidl_handle& fence_handle, int* out_fence_fd);

int convertToFenceHandle(int fence_fd, android::hardware::hidl_handle* out_fence_handle);

int getPlaneLayouts(
        uint32_t drm_format,
        std::vector<aidl::android::hardware::graphics::common::PlaneLayout>* out_layouts);
