/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <android/hardware/graphics/allocator/4.0/IAllocator.h>
#include <android/hardware/graphics/mapper/4.0/IMapper.h>

#include "gralloc_driver.h"

class NxpAllocator : public android::hardware::graphics::allocator::V4_0::IAllocator {
  public:
    NxpAllocator();

    android::hardware::Return<void> allocate(const android::hardware::hidl_vec<uint8_t>& descriptor,
                                             uint32_t count, allocate_cb hidl_cb) override;

  private:
    android::hardware::graphics::mapper::V4_0::Error allocate(
            const android::hardware::graphics::mapper::V4_0::IMapper::BufferDescriptorInfo&
                    description,
            uint32_t* outStride, const native_handle_t** /*android::hardware::hidl_handle* */outHandle);

    std::unique_ptr<gralloc_driver> mDriver;
};
