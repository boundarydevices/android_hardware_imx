/*
 * Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Copyright 2023 NXP
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "NxpAllocator.h"

#include <aidl/android/hardware/graphics/allocator/AllocationError.h>
#include <aidlcommonsupport/NativeHandle.h>
#include <android-base/logging.h>
#include <android/binder_ibinder_platform.h>
#include <gralloctypes/Gralloc4.h>
#include <log/log.h>

#include "NxpUtils.h"

using aidl::android::hardware::common::NativeHandle;
using BufferDescriptorInfoV4 =
        android::hardware::graphics::mapper::V4_0::IMapper::BufferDescriptorInfo;

namespace aidl::android::hardware::graphics::allocator::impl {
namespace {

inline ndk::ScopedAStatus ToBinderStatus(AllocationError error) {
    return ndk::ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(error));
}

} // namespace

bool NxpAllocator::init() {
    mDriver = gralloc_driver::get_instance();
    return mDriver != nullptr;
}

ndk::ScopedAStatus NxpAllocator::initializeMetadata(
        gralloc_handle_t memHandle, const struct gralloc_buffer_descriptor& memDescriptor) {
    if (!mDriver) {
        ALOGE("Failed to initializeMetadata. Driver is uninitialized.\n");
        return ToBinderStatus(AllocationError::NO_RESOURCES);
    }

    if (!memHandle) {
        ALOGE("Failed to initializeMetadata. Invalid handle.\n");
        return ToBinderStatus(AllocationError::NO_RESOURCES);
    }

    void* addr;
    uint64_t size;
    int ret = mDriver->get_reserved_region(memHandle, &addr, &size);
    if (ret) {
        ALOGE("Failed to getReservedRegion.\n");
        return ToBinderStatus(AllocationError::NO_RESOURCES);
    }

    gralloc_metadata* memMetadata = reinterpret_cast<gralloc_metadata*>(addr);

    snprintf(memMetadata->name, GRALLOC_METADATA_MAX_NAME_SIZE, "%s", memDescriptor.name.c_str());
    memMetadata->dataspace = common::Dataspace::UNKNOWN;
    memMetadata->blendMode = common::BlendMode::INVALID;

    return ndk::ScopedAStatus::ok();
}

void NxpAllocator::releaseBufferAndHandle(native_handle_t* handle) {
    mDriver->release(handle);
    //    native_handle_close(handle);
    //    native_handle_delete(handle);
}

ndk::ScopedAStatus NxpAllocator::allocate(const std::vector<uint8_t>& descriptor, int32_t count,
                                          allocator::AllocationResult* outResult) {
    if (!mDriver) {
        ALOGE("Failed to allocate. Driver is uninitialized.\n");
        return ToBinderStatus(AllocationError::NO_RESOURCES);
    }

    BufferDescriptorInfoV4 description;

    int ret = ::android::gralloc4::decodeBufferDescriptorInfo(descriptor, &description);
    if (ret) {
        ALOGE("Failed to allocate. Failed to decode buffer descriptor: %d.\n", ret);
        return ToBinderStatus(AllocationError::BAD_DESCRIPTOR);
    }

    std::vector<native_handle_t*> handles;
    handles.resize(count, nullptr);

    for (int32_t i = 0; i < count; i++) {
        ndk::ScopedAStatus status = allocate(description, &outResult->stride, &handles[i]);
        if (!status.isOk()) {
            for (int32_t j = 0; j < i; j++) {
                releaseBufferAndHandle(handles[j]);
            }
            return status;
        }
    }

    outResult->buffers.resize(count);
    for (int32_t i = 0; i < count; i++) {
        auto handle = handles[i];
        outResult->buffers[i] = ::android::dupToAidl(handle);
        releaseBufferAndHandle(handle);
    }

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus NxpAllocator::allocate(const BufferDescriptorInfoV4& descriptor,
                                          int32_t* outStride, native_handle_t** outHandle) {
    if (!mDriver) {
        ALOGE("Failed to allocate. Driver is uninitialized.\n");
        return ToBinderStatus(AllocationError::NO_RESOURCES);
    }

    struct gralloc_buffer_descriptor memDescriptor;
    if (convertToMemDescriptor(descriptor, &memDescriptor)) {
        return ToBinderStatus(AllocationError::UNSUPPORTED);
    }

    memDescriptor.reserved_region_size += sizeof(gralloc_metadata);

    if (!mDriver->is_supported(&memDescriptor)) {
        const std::string drmFormatString = getDrmFormatString(memDescriptor.drm_format);
        const std::string pixelFormatString = getPixelFormatString(descriptor.format);
        const std::string usageString = getUsageString(descriptor.usage);
        ALOGE("Failed to allocate. Unsupported combination: pixel format:%s, drm format:%s, "
              "usage:%s\n",
              pixelFormatString.c_str(), drmFormatString.c_str(), usageString.c_str());
        return ToBinderStatus(AllocationError::UNSUPPORTED);
    }

    native_handle_t* handle;
    int ret = mDriver->allocate(&memDescriptor, &handle);
    if (ret) {
        return ToBinderStatus(AllocationError::NO_RESOURCES);
    }

    gralloc_handle_t memHandle = gralloc_convert_handle(handle);

    auto status = initializeMetadata(memHandle, memDescriptor);
    if (!status.isOk()) {
        ALOGE("Failed to allocate. Failed to initialize gralloc buffer metadata.");
        releaseBufferAndHandle(handle);
        return status;
    }

    *outStride = static_cast<int32_t>(memHandle->stride);
    *outHandle = handle;

    return ndk::ScopedAStatus::ok();
}

::ndk::SpAIBinder NxpAllocator::createBinder() {
    auto binder = BnAllocator::createBinder();
    AIBinder_setInheritRt(binder.get(), true);
    return binder;
}

} // namespace aidl::android::hardware::graphics::allocator::impl
