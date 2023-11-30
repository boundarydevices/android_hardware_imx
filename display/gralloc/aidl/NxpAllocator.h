/*
 * Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Copyright 2023 NXP
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef NXP_GRALLOC_AIDL_ALLOCATOR_H_
#define NXP_GRALLOC_AIDL_ALLOCATOR_H_

#include <DisplayUtil.h>
#include <aidl/android/hardware/graphics/allocator/AllocationResult.h>
#include <aidl/android/hardware/graphics/allocator/BnAllocator.h>
#include <android/hardware/graphics/mapper/4.0/IMapper.h>

#include "gralloc_driver.h"
#include "gralloc_helpers.h"
#include "gralloc_metadata.h"

namespace aidl::android::hardware::graphics::allocator::impl {

class NxpAllocator : public BnAllocator {
public:
    NxpAllocator() = default;

    bool init();

    ndk::ScopedAStatus allocate(const std::vector<uint8_t>& descriptor, int32_t count,
                                allocator::AllocationResult* outResult) override;

protected:
    ndk::SpAIBinder createBinder() override;

private:
    ndk::ScopedAStatus allocate(
            const ::android::hardware::graphics::mapper::V4_0::IMapper::BufferDescriptorInfo&
                    descriptor,
            int32_t* outStride, native_handle_t** outHandle);

    ndk::ScopedAStatus initializeMetadata(gralloc_handle_t memHandle,
                                          const struct gralloc_buffer_descriptor& memDescriptor);

    void releaseBufferAndHandle(native_handle_t* handle);

    gralloc_driver* mDriver = nullptr;
};

} // namespace aidl::android::hardware::graphics::allocator::impl

#endif
