/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "NxpAllocator.h"

#include <android/hardware/graphics/mapper/4.0/IMapper.h>
#include <gralloctypes/Gralloc4.h>

#include <cutils/log.h>
#include <Memory.h>
#include "gralloc_helpers.h"
#include "NxpUtils.h"

using android::hardware::hidl_handle;
using android::hardware::hidl_vec;
using android::hardware::Return;
using android::hardware::Void;
using android::hardware::graphics::common::V1_2::BufferUsage;
using android::hardware::graphics::common::V1_2::PixelFormat;
using android::hardware::graphics::mapper::V4_0::Error;

using BufferDescriptorInfo =
        android::hardware::graphics::mapper::V4_0::IMapper::BufferDescriptorInfo;

NxpAllocator::NxpAllocator() : mDriver(std::make_unique<gralloc_driver>()) {
    if (mDriver->init()) {
        ALOGE("Failed to initialize driver.");
        mDriver = nullptr;
    }
}

Error NxpAllocator::allocate(const BufferDescriptorInfo& descriptor, uint32_t* outStride,
                                      const native_handle_t** /* hidl_handle* */ outHandle) {
    if (!mDriver) {
        ALOGE("%s Driver is uninitialized.", __func__);
        return Error::NO_RESOURCES;
    }

    if (!outStride || !outHandle) {
        return Error::NO_RESOURCES;
    }

    struct gralloc_buffer_descriptor memDescriptor;
    if (convertToMemDescriptor(descriptor, &memDescriptor)) {
        return Error::UNSUPPORTED;
    }

    bool supported = mDriver->is_supported(&memDescriptor);
    if (!supported && (descriptor.usage & BufferUsage::COMPOSER_OVERLAY)) {
        memDescriptor.use_flags &= ~BO_USE_SCANOUT;
        supported = mDriver->is_supported(&memDescriptor);
    }

    if (!supported) {
        std::string drmFormatString = getDrmFormatString(memDescriptor.drm_format);
        std::string pixelFormatString = getPixelFormatString(descriptor.format);
        std::string usageString = getUsageString(descriptor.usage);
        ALOGE("%s Unsupported combination -- pixel format: %s, drm format:%s, usage: %s", __func__,
                pixelFormatString.c_str(), drmFormatString.c_str(), usageString.c_str());
        return Error::UNSUPPORTED;
    }

    buffer_handle_t handle;
    int ret = mDriver->allocate(&memDescriptor, &handle);
    if (ret) {
        return Error::NO_RESOURCES;
    }

    gralloc_handle_t memHandle = gralloc_convert_handle(handle);
    if (!memHandle) {
        return Error::NO_RESOURCES;
    }

    *outHandle = handle;
    *outStride = memHandle->stride;

    return Error::NONE;
}

Return<void> NxpAllocator::allocate(const hidl_vec<uint8_t>& descriptor, uint32_t count,
                                             allocate_cb hidlCb) {
    hidl_vec<hidl_handle> handles;

    if (!mDriver) {
        ALOGE("%s Driver is not initialized.", __func__);
        hidlCb(Error::NO_RESOURCES, 0, handles);
        return Void();
    }

    BufferDescriptorInfo description;

    int ret = android::gralloc4::decodeBufferDescriptorInfo(descriptor, &description);
    if (ret) {
        ALOGE("%s Failed to decode buffer descriptor: %d.", __func__, ret);
        hidlCb(Error::BAD_DESCRIPTOR, 0, handles);
        return Void();
    }

    std::vector<const native_handle_t*> buffers;
    buffers.resize(count);
    uint32_t stride = 0;
    for (int i = 0; i < buffers.size(); i++) {
        Error err = allocate(description, &stride, &(buffers[i]));
        if (err != Error::NONE) {
            for (int j = 0; j < i; j++) {
                mDriver->release(buffers[i]);
            }
            hidlCb(err, 0, handles);
            return Void();
        }
    }

    hidl_vec<hidl_handle> hidlBuffers(buffers.cbegin(), buffers.cend());
    hidlCb(Error::NONE, stride, hidlBuffers);

    for (const hidl_handle& handle : hidlBuffers) {
        mDriver->release(handle.getNativeHandle());
    }

    return Void();
}
