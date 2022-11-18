/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <inttypes.h>
#include <aidl/android/hardware/graphics/common/BlendMode.h>
#include <aidl/android/hardware/graphics/common/Dataspace.h>
#include <aidl/android/hardware/graphics/common/PlaneLayout.h>
#include <aidl/android/hardware/graphics/common/Rect.h>
#include <cutils/native_handle.h>
#include <cutils/log.h>
#include <gralloctypes/Gralloc4.h>

#include <Memory.h>
#include <DisplayUtil.h>

#include "NxpMapper.h"
#include "NxpUtils.h"
#include "helpers.h"

using aidl::android::hardware::graphics::common::BlendMode;
using aidl::android::hardware::graphics::common::Dataspace;
using aidl::android::hardware::graphics::common::PlaneLayout;
using aidl::android::hardware::graphics::common::Rect;
using android::hardware::hidl_handle;
using android::hardware::hidl_vec;
using android::hardware::Return;
using android::hardware::Void;
using android::hardware::graphics::common::V1_2::BufferUsage;
using android::hardware::graphics::common::V1_2::PixelFormat;
using android::hardware::graphics::mapper::V4_0::Error;
using android::hardware::graphics::mapper::V4_0::IMapper;

NxpMapper::NxpMapper() : mDriver(std::make_unique<gralloc_driver>()) {
    if (mDriver->init()) {
        ALOGE("Failed to initialize driver.");
        mDriver = nullptr;
    }
}

Return<void> NxpMapper::createDescriptor(const BufferDescriptorInfo& description,
                                                  createDescriptor_cb hidlCb) {
    hidl_vec<uint8_t> descriptor;

    if ((description.width == 0) || (description.height == 0) || (description.layerCount == 0)) {
        ALOGE("%s Bad parameters, width: %d, height: %d, layer count: %d.", __func__,
                 description.width, description.height, description.layerCount);
        hidlCb(Error::BAD_VALUE, descriptor);
        return Void();
    }

    if (description.layerCount != 1) {
         ALOGE("%s layerCount=%d > 1 is unsupported", __func__, description.layerCount);
         hidlCb(Error::UNSUPPORTED, descriptor);
         return Void();
    }

    if (description.format == static_cast<PixelFormat>(0)) {
        ALOGE("%s Bad parameter, format=0x%x", __func__, description.format);
        hidlCb(Error::BAD_VALUE, descriptor);
        return Void();
    }

    int ret = android::gralloc4::encodeBufferDescriptorInfo(description, &descriptor);
    if (ret) {
        ALOGE("%s Failed to encode: %d.", __func__, ret);
        hidlCb(Error::BAD_VALUE, descriptor);
        return Void();
    }

    hidlCb(Error::NONE, descriptor);
    return Void();
}

Return<void> NxpMapper::importBuffer(const hidl_handle& handle, importBuffer_cb hidlCb) {
    if (!mDriver) {
        ALOGE("%s Driver is uninitialized.", __func__);
        hidlCb(Error::NO_RESOURCES, nullptr);
        return Void();
    }

    const native_handle_t* bufferHandle = handle.getNativeHandle();
    if (!bufferHandle || bufferHandle->numFds == 0) {
        ALOGE("%s Bad handle", __func__);
        hidlCb(Error::BAD_BUFFER, nullptr);
        return Void();
    }

    native_handle_t* importedBufferHandle = native_handle_clone(bufferHandle);
    if (!importedBufferHandle) {
        ALOGE("%s Handle clone failed", __func__);
        hidlCb(Error::NO_RESOURCES, nullptr);
        return Void();
    }

    int ret = mDriver->retain(importedBufferHandle);
    if (ret) {
        native_handle_close(importedBufferHandle);
        native_handle_delete(importedBufferHandle);
        hidlCb(Error::NO_RESOURCES, nullptr);
        return Void();
    }

    void* buffer = addImportedBuffer(importedBufferHandle);
    if (!buffer) {
        mDriver->release(importedBufferHandle);
        hidlCb(Error::NO_RESOURCES, nullptr);
        return Void();
    }

    hidlCb(Error::NONE, buffer);
    return Void();
}

Return<Error> NxpMapper::freeBuffer(void* rawHandle) {
    if (!mDriver) {
        ALOGE("%s Driver is uninitialized.", __func__);
        return Error::NO_RESOURCES;
    }

    native_handle_t* bufferHandle = getImportedBuffer(rawHandle);
    if (!bufferHandle) {
        ALOGE("%s Empty handle", __func__);
        return Error::BAD_BUFFER;
    }

    removeImportedBuffer(rawHandle);
    int ret = mDriver->release(bufferHandle);
    if (ret) {
        return Error::BAD_BUFFER;
    }

    // TODO: The fd close operation and delete handle are done in ~gralloc_buffer()
    //native_handle_close(bufferHandle);
    //native_handle_delete(bufferHandle);
    return Error::NONE;
}

Return<Error> NxpMapper::validateBufferSize(void* rawHandle,
                                                     const BufferDescriptorInfo& descriptor,
                                                     uint32_t stride) {
    if (!mDriver) {
        ALOGE("%s Driver is uninitialized.", __func__);
        return Error::NO_RESOURCES;
    }

    const native_handle_t* bufferHandle = getImportedBuffer(rawHandle);
    if (!bufferHandle) {
        ALOGE("%s Empty handle.", __func__);
        return Error::BAD_BUFFER;
    }

    gralloc_handle_t memHandle = gralloc_convert_handle(bufferHandle);
    if (!memHandle) {
        ALOGE("%s Invalid handle.", __func__);
        return Error::BAD_BUFFER;
    }

    if (descriptor.layerCount != 1) {
        ALOGE("%s layerCount only support 1, but is %d.", __func__, descriptor.layerCount);
        return Error::BAD_VALUE;
    }

    /* gralloc driver will change some format to HAL_PIXEL_FORMAT_YCbCr_420_SP, so not check format*/
/*    PixelFormat memHandleFormat = static_cast<PixelFormat>(memHandle->format);
    if (descriptor.format != memHandleFormat) {
        ALOGE("%s Format mismatch (0x%x vs 0x%x).", __func__, descriptor.format, memHandleFormat);
        return Error::BAD_BUFFER;
    }
*/

    if (descriptor.width != memHandle->width) {
        ALOGE("%s Width mismatch (%d vs %d).", __func__, descriptor.width, memHandle->width);
        return Error::BAD_VALUE;
    }

    if (descriptor.height != memHandle->height) {
        ALOGE("%s Height mismatch (%d vs %d).", __func__, descriptor.height, memHandle->height);
        return Error::BAD_VALUE;
    }

    if (stride != memHandle->stride) {
        ALOGE("%s Stride mismatch (%d vs %d).", __func__, stride, memHandle->stride);
        return Error::BAD_VALUE;
    }

    struct gralloc_buffer_descriptor memDescriptor;
    if (convertToMemDescriptor(descriptor, &memDescriptor)) {
        return Error::UNSUPPORTED;
    }

    int ret = mDriver->validate_buffer(&memDescriptor, bufferHandle);
    if (ret) {
        ALOGE("%s Validate buffer size failed", __func__);
        return Error::BAD_VALUE;
    }

    return Error::NONE;
}

Return<void> NxpMapper::getTransportSize(void* rawHandle, getTransportSize_cb hidlCb) {
    if (!mDriver) {
        ALOGE("%s Driver is uninitialized.", __func__);
        hidlCb(Error::BAD_BUFFER, 0, 0);
        return Void();
    }

    const native_handle_t* bufferHandle = getImportedBuffer(rawHandle);
    if (!bufferHandle) {
        ALOGE("%s Bad handle.", __func__);
        hidlCb(Error::BAD_BUFFER, 0, 0);
        return Void();
    }

    // No local process data is currently stored on the native handle.
    hidlCb(Error::NONE, bufferHandle->numFds, bufferHandle->numInts);
    return Void();
}

Return<void> NxpMapper::lock(void* rawBuffer, uint64_t cpuUsage, const Rect& region,
                                      const hidl_handle& acquireFence, lock_cb hidlCb) {
    if (!mDriver) {
        ALOGE("%s Driver is uninitialized.", __func__);
        hidlCb(Error::NO_RESOURCES, nullptr);
        return Void();
    }

    const native_handle_t* bufferHandle = getConstImportedBuffer(rawBuffer);
    if (!bufferHandle) {
        ALOGE("%s Empty handle.", __func__);
        hidlCb(Error::BAD_BUFFER, nullptr);
        return Void();
    }

    if (cpuUsage == 0) {
        ALOGE("%s Bad cpu usage: %" PRIu64 ".", __func__, cpuUsage);
        hidlCb(Error::BAD_VALUE, nullptr);
        return Void();
    }

    uint32_t mapUsage = 0;
    int ret = convertToMapUsage(cpuUsage, &mapUsage);
    if (ret) {
        ALOGE("%s Convert usage failed.", __func__);
        hidlCb(Error::BAD_VALUE, nullptr);
        return Void();
    }
    gralloc_handle_t memHandle = gralloc_convert_handle(bufferHandle);
    if (memHandle == nullptr) {
        ALOGE("%s Invalid handle.", __func__);
        hidlCb(Error::BAD_VALUE, nullptr);
        return Void();
    }

    if ((region.left < 0) || (region.top < 0) || (region.width < 0) || (region.height < 0)
          || (region.width > memHandle->width) || (region.height > memHandle->height)) {
        ALOGE("%s Invalid region: left= %d, top= %d, width= %d, height= %d", __func__,
               region.left, region.top, region.width, region.height);
        hidlCb(Error::BAD_VALUE, nullptr);
        return Void();
    }

    struct rectangle rect = {static_cast<uint32_t>(region.left), static_cast<uint32_t>(region.top),
                             static_cast<uint32_t>(region.width),
                             static_cast<uint32_t>(region.height)};

    // An access region of all zeros means the entire buffer.
    if (rect.x == 0 && rect.y == 0 && rect.width == 0 && rect.height == 0) {
        rect.width = memHandle->width;
        rect.height = memHandle->height;
    }

    int acquireFenceFd = -1;
    ret = convertToFenceFd(acquireFence, &acquireFenceFd);
    if (ret) {
        ALOGE("%s Bad acquire fence", __func__);
        hidlCb(Error::BAD_VALUE, nullptr);
        return Void();
    }

    uint8_t* addr[DRV_MAX_PLANES];
    ret = mDriver->lock(bufferHandle, acquireFenceFd, /*close_acquire_fence=*/false, &rect,
                        mapUsage, addr);
    if (ret) {
        ALOGE("%s Lock buffer handle failed", __func__);
        hidlCb(Error::BAD_VALUE, nullptr);
        return Void();
    }

    hidlCb(Error::NONE, addr[0]);
    return Void();
}

Return<void> NxpMapper::unlock(void* rawHandle, unlock_cb hidlCb) {
    if (!mDriver) {
        ALOGE("%s Driver is uninitialized.", __func__);
        hidlCb(Error::BAD_BUFFER, nullptr);
        return Void();
    }

    const native_handle_t* bufferHandle = getConstImportedBuffer(rawHandle);
    if (!bufferHandle) {
        ALOGE("%s Empty handle.", __func__);
        hidlCb(Error::BAD_BUFFER, nullptr);
        return Void();
    }

    int releaseFenceFd = -1;
    int ret = mDriver->unlock(bufferHandle, &releaseFenceFd);
    if (ret) {
        ALOGE("%s Failed to unlock.", __func__);
        hidlCb(Error::BAD_BUFFER, nullptr);
        return Void();
    }

    hidl_handle releaseFenceHandle;
    ret = convertToFenceHandle(releaseFenceFd, &releaseFenceHandle);
    if (ret) {
        ALOGE("%s Failed to convert release fence to handle.", __func__);
        hidlCb(Error::BAD_BUFFER, nullptr);
        return Void();
    }

    hidlCb(Error::NONE, releaseFenceHandle);
    return Void();
}

Return<void> NxpMapper::flushLockedBuffer(void* rawHandle, flushLockedBuffer_cb hidlCb) {
    if (!mDriver) {
        ALOGE("%s Driver is uninitialized.", __func__);
        hidlCb(Error::NO_RESOURCES, nullptr);
        return Void();
    }

    const native_handle_t* bufferHandle = getConstImportedBuffer(rawHandle);
    if (!bufferHandle) {
        ALOGE("%s Empty handle.", __func__);
        hidlCb(Error::BAD_BUFFER, nullptr);
        return Void();
    }

    int releaseFenceFd = -1;
    int ret = mDriver->flush(bufferHandle, &releaseFenceFd);
    if (ret) {
        ALOGE("%s Flush failed.", __func__);
        hidlCb(Error::BAD_BUFFER, nullptr);
        return Void();
    }

    hidl_handle releaseFenceHandle;
    ret = convertToFenceHandle(releaseFenceFd, &releaseFenceHandle);
    if (ret) {
        ALOGE("%s Failed to convert release fence to handle.", __func__);
        hidlCb(Error::BAD_BUFFER, nullptr);
        return Void();
    }

    hidlCb(Error::NONE, releaseFenceHandle);
    return Void();
}

Return<Error> NxpMapper::rereadLockedBuffer(void* rawHandle) {
    if (!mDriver) {
        ALOGE("%s Driver is uninitialized.", __func__);
        return Error::NO_RESOURCES;
    }

    const native_handle_t* bufferHandle = getConstImportedBuffer(rawHandle);
    if (!bufferHandle) {
        ALOGE("%s Empty handle.", __func__);
        return Error::BAD_BUFFER;
    }

    int ret = mDriver->invalidate(bufferHandle);
    if (ret) {
        ALOGE("%s Failed to invalidate.", __func__);
        return Error::BAD_BUFFER;
    }

    return Error::NONE;
}

Return<void> NxpMapper::isSupported(const BufferDescriptorInfo& descriptor,
                                             isSupported_cb hidlCb) {
    if (!mDriver) {
        ALOGE("%s Driver is uninitialized.", __func__);
        hidlCb(Error::BAD_VALUE, false);
        return Void();
    }

    if (descriptor.layerCount != 1) {
         ALOGE("%s layerCount=%d != 1 is unsupported", __func__, descriptor.layerCount);
         hidlCb(Error::NONE, false);
         return Void();
    }

    struct gralloc_buffer_descriptor memDescriptor;
    if (convertToMemDescriptor(descriptor, &memDescriptor)) {
        hidlCb(Error::NONE, false);
        return Void();
    }

    bool supported = mDriver->is_supported(&memDescriptor);
    if (!supported) {
        memDescriptor.use_flags &= ~BO_USE_SCANOUT;
        supported = mDriver->is_supported(&memDescriptor);
    }

    hidlCb(Error::NONE, supported);
    return Void();
}

Return<void> NxpMapper::get(void* rawHandle, const MetadataType& metadataType,
                                     get_cb hidlCb) {
    hidl_vec<uint8_t> encodedMetadata;

    if (!mDriver) {
        ALOGE("%s Driver is uninitialized.", __func__);
        hidlCb(Error::NO_RESOURCES, encodedMetadata);
        return Void();
    }

    const native_handle_t* bufferHandle = getConstImportedBuffer(rawHandle);
    if (!bufferHandle) {
        ALOGE("%s Empty handle.", __func__);
        hidlCb(Error::BAD_BUFFER, encodedMetadata);
        return Void();
    }

    gralloc_handle_t memHandle = gralloc_convert_handle(bufferHandle);
    if (!memHandle) {
        ALOGE("%s Invalid handle.", __func__);
        hidlCb(Error::BAD_BUFFER, encodedMetadata);
        return Void();
    }

    get(memHandle, metadataType, hidlCb);
    return Void();
}

Return<void> NxpMapper::get(gralloc_handle_t memHandle, const MetadataType& metadataType, get_cb hidlCb) {
    hidl_vec<uint8_t> encodedMetadata;

    if (!mDriver) {
        ALOGE("%s Driver is uninitialized.", __func__);
        hidlCb(Error::NO_RESOURCES, encodedMetadata);
        return Void();
    }

    if (!memHandle) {
        ALOGE("%s Invalid handle.", __func__);
        hidlCb(Error::BAD_BUFFER, encodedMetadata);
        return Void();
    }

    const gralloc_metadata* grallocMetadata = nullptr;
    if (metadataType == android::gralloc4::MetadataType_BlendMode ||
        metadataType == android::gralloc4::MetadataType_Cta861_3 ||
        metadataType == android::gralloc4::MetadataType_Dataspace ||
        metadataType == android::gralloc4::MetadataType_Smpte2086) {
        Error error = getMetadata(memHandle, &grallocMetadata);
        if (error != Error::NONE) {
            ALOGI("Failed to get. Failed to get buffer metadata.\n");
            hidlCb(Error::NO_RESOURCES, encodedMetadata);
            return Void();
        }
    }

    android::status_t status = android::NO_ERROR;
    if (metadataType == android::gralloc4::MetadataType_BufferId) {
        status = android::gralloc4::encodeBufferId(memHandle->id, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Name) {
        const char* name = (const char*)(memHandle->name);
        status = android::gralloc4::encodeName(name, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Width) {
        status = android::gralloc4::encodeWidth(memHandle->width, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Height) {
        status = android::gralloc4::encodeHeight(memHandle->height, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_LayerCount) {
        status = android::gralloc4::encodeLayerCount(1, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_PixelFormatRequested) {
        PixelFormat pixelFormat = static_cast<PixelFormat>(memHandle->format);
        status = android::gralloc4::encodePixelFormatRequested(pixelFormat, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_PixelFormatFourCC) {
        uint32_t drm_format = drv_convert_nxp_format_to_drm_format(memHandle->format);
        status = android::gralloc4::encodePixelFormatFourCC(drm_format, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_PixelFormatModifier) {
        status = android::gralloc4::encodePixelFormatModifier(memHandle->format_modifier,
                                                              &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Usage) {
        uint64_t usage = static_cast<uint64_t>(memHandle->usage);
        status = android::gralloc4::encodeUsage(usage, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_AllocationSize) {
        status = android::gralloc4::encodeAllocationSize(memHandle->total_size, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_ProtectedContent) {
        uint64_t hasProtectedContent = memHandle->usage & BufferUsage::PROTECTED ? 1 : 0;
        status = android::gralloc4::encodeProtectedContent(hasProtectedContent, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Compression) {
        status = android::gralloc4::encodeCompression(android::gralloc4::Compression_None,
                                                      &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Interlaced) {
        status = android::gralloc4::encodeInterlaced(android::gralloc4::Interlaced_None,
                                                     &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_ChromaSiting) {
        status = android::gralloc4::encodeChromaSiting(android::gralloc4::ChromaSiting_None,
                                                       &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_PlaneLayouts) {
        std::vector<PlaneLayout> planeLayouts;
        uint32_t drm_format = drv_convert_nxp_format_to_drm_format(memHandle->fslFormat);
        getPlaneLayouts(drm_format, &planeLayouts);
        for (size_t plane = 0; plane < planeLayouts.size(); plane++) {
            PlaneLayout& planeLayout = planeLayouts[plane];
            planeLayout.offsetInBytes = memHandle->offsets[plane];
            planeLayout.strideInBytes = memHandle->strides[plane];
            planeLayout.totalSizeInBytes = memHandle->sizes[plane];
            planeLayout.widthInSamples = memHandle->width / planeLayout.horizontalSubsampling;
            planeLayout.heightInSamples = memHandle->height / planeLayout.verticalSubsampling;
        }

        status = android::gralloc4::encodePlaneLayouts(planeLayouts, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Crop) {
        std::vector<aidl::android::hardware::graphics::common::Rect> crops;
        for (size_t plane = 0; plane < memHandle->num_planes; plane++) {
            aidl::android::hardware::graphics::common::Rect crop;
            crop.left = 0;
            crop.top = 0;
            crop.right = memHandle->width;
            crop.bottom = memHandle->height;
            crops.push_back(crop);
        }

        status = android::gralloc4::encodeCrop(crops, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Dataspace) {
        status = android::gralloc4::encodeDataspace(grallocMetadata->dataspace, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_BlendMode) {
        status = android::gralloc4::encodeBlendMode(grallocMetadata->blendMode, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Smpte2086) {
        status = android::gralloc4::encodeSmpte2086(grallocMetadata->smpte2086, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Cta861_3) {
        status = android::gralloc4::encodeCta861_3(grallocMetadata->cta861_3, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Smpte2094_40) {
        status = android::gralloc4::encodeSmpte2094_40(std::nullopt, &encodedMetadata);
    } else {
        hidlCb(Error::UNSUPPORTED, encodedMetadata);
        return Void();
    }

    if (status != android::NO_ERROR) {
        hidlCb(Error::NO_RESOURCES, encodedMetadata);
        ALOGE("%s Failed to encode metadata.", __func__);
        return Void();
    }

    hidlCb(Error::NONE, encodedMetadata);
    return Void();
}

Return<Error> NxpMapper::set(void* rawHandle, const MetadataType& metadataType,
                                      const hidl_vec<uint8_t>& encodedMetadata) {
    if (!mDriver) {
        ALOGE("%s Driver is uninitialized.", __func__);
        return Error::NO_RESOURCES;
    }

    const native_handle_t* bufferHandle = getConstImportedBuffer(rawHandle);
    if (!bufferHandle) {
        ALOGE("%s Empty handle.", __func__);
        return Error::BAD_BUFFER;
    }

    gralloc_handle_t memHandle = gralloc_convert_handle(bufferHandle);
    if (!memHandle) {
        ALOGE("%s Invalid handle.", __func__);
        return Error::BAD_BUFFER;
    }

    if (metadataType == android::gralloc4::MetadataType_BufferId) {
        return Error::BAD_VALUE;
    } else if (metadataType == android::gralloc4::MetadataType_Name) {
        return Error::BAD_VALUE;
    } else if (metadataType == android::gralloc4::MetadataType_Width) {
        return Error::BAD_VALUE;
    } else if (metadataType == android::gralloc4::MetadataType_Height) {
        return Error::BAD_VALUE;
    } else if (metadataType == android::gralloc4::MetadataType_LayerCount) {
        return Error::BAD_VALUE;
    } else if (metadataType == android::gralloc4::MetadataType_PixelFormatRequested) {
        return Error::BAD_VALUE;
    } else if (metadataType == android::gralloc4::MetadataType_Usage) {
        return Error::BAD_VALUE;
    }

    if (metadataType != android::gralloc4::MetadataType_BlendMode &&
        metadataType != android::gralloc4::MetadataType_Cta861_3 &&
        metadataType != android::gralloc4::MetadataType_Dataspace &&
        metadataType != android::gralloc4::MetadataType_Smpte2086) {
        return Error::UNSUPPORTED;
    }
    Error error = Error::NONE;
    error = set(memHandle,metadataType, encodedMetadata);
    return error;
}

Error NxpMapper::set(gralloc_handle_t memHandle, const MetadataType& metadataType,
                              const android::hardware::hidl_vec<uint8_t>& encodedMetadata) {
    if (!mDriver) {
        ALOGI("Failed to set. Driver is uninitialized.");
        return Error::NO_RESOURCES;
    }
    if (!memHandle) {
        ALOGI("Failed to set. Invalid buffer.");
        return Error::BAD_BUFFER;
    }
    gralloc_metadata* grallocMetadata = nullptr;
    Error error = getMutableMetadata(memHandle, &grallocMetadata);
    if (error != Error::NONE) {
        ALOGI("Failed to set. Failed to get buffer metadata.");
        return Error::UNSUPPORTED;
    }
    if (metadataType == android::gralloc4::MetadataType_BlendMode) {
        auto status = android::gralloc4::decodeBlendMode(encodedMetadata, &grallocMetadata->blendMode);
        if (status != android::NO_ERROR) {
            ALOGI("Failed to set. Failed to decode blend mode.");
            return Error::UNSUPPORTED;
        }
    } else if (metadataType == android::gralloc4::MetadataType_Cta861_3) {
        auto status = android::gralloc4::decodeCta861_3(encodedMetadata, &grallocMetadata->cta861_3);
        if (status != android::NO_ERROR) {
            ALOGI("Failed to set. Failed to decode cta861_3.");
            return Error::UNSUPPORTED;
        }
    } else if (metadataType == android::gralloc4::MetadataType_Dataspace) {
        auto status = android::gralloc4::decodeDataspace(encodedMetadata, &grallocMetadata->dataspace);
        if (status != android::NO_ERROR) {
            ALOGI("Failed to set. Failed to decode dataspace.");
            return Error::UNSUPPORTED;
        }
    } else if (metadataType == android::gralloc4::MetadataType_Smpte2086) {
        auto status = android::gralloc4::decodeSmpte2086(encodedMetadata, &grallocMetadata->smpte2086);
        if (status != android::NO_ERROR) {
            ALOGI("Failed to set. Failed to decode smpte2086.");
            return Error::UNSUPPORTED;
        }
    }
    return Error::NONE;
}

int NxpMapper::getResolvedDrmFormat(PixelFormat pixelFormat, uint64_t bufferUsage,
                                             uint32_t* outDrmFormat) {
    uint32_t drmFormat;
    if (convertToDrmFormat(pixelFormat, &drmFormat)) {
        std::string pixelFormatString = getPixelFormatString(pixelFormat);
        ALOGE("%s Failed to convert format %s", __func__, pixelFormatString.c_str());
        return -1;
    }

    uint64_t usage;
    if (convertToBufferUsage(bufferUsage, &usage)) {
        std::string usageString = getUsageString(bufferUsage);
        ALOGE("%s Failed to convert usage %s", __func__, usageString.c_str());
        return -1;
    }

    uint32_t resolvedDrmFormat = mDriver->get_resolved_drm_format(drmFormat, usage);
    if (resolvedDrmFormat == DRM_FORMAT_INVALID) {
        std::string drmFormatString = getDrmFormatString(drmFormat);
        ALOGE("%s Failed to resolve drm format %s", __func__, drmFormatString.c_str());
        return -1;
    }

    *outDrmFormat = resolvedDrmFormat;

    return 0;
}

Return<void> NxpMapper::getFromBufferDescriptorInfo(
        const BufferDescriptorInfo& descriptor, const MetadataType& metadataType,
        getFromBufferDescriptorInfo_cb hidlCb) {
    hidl_vec<uint8_t> encodedMetadata;

    if (!mDriver) {
        ALOGE("%s Driver is uninitialized.", __func__);
        hidlCb(Error::NO_RESOURCES, encodedMetadata);
        return Void();
    }

    android::status_t status = android::NO_ERROR;
    if (metadataType == android::gralloc4::MetadataType_Name) {
        status = android::gralloc4::encodeName(descriptor.name, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Width) {
        status = android::gralloc4::encodeWidth(descriptor.width, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Height) {
        status = android::gralloc4::encodeHeight(descriptor.height, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_LayerCount) {
        status = android::gralloc4::encodeLayerCount(1, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_PixelFormatRequested) {
        status = android::gralloc4::encodePixelFormatRequested(descriptor.format, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_PixelFormatFourCC) {
        uint32_t drmFormat;
        if (getResolvedDrmFormat(descriptor.format, descriptor.usage, &drmFormat)) {
            hidlCb(Error::BAD_VALUE, encodedMetadata);
            return Void();
        }
        status = android::gralloc4::encodePixelFormatFourCC(drmFormat, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Usage) {
        status = android::gralloc4::encodeUsage(descriptor.usage, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_ProtectedContent) {
        uint64_t hasProtectedContent = descriptor.usage & BufferUsage::PROTECTED ? 1 : 0;
        status = android::gralloc4::encodeProtectedContent(hasProtectedContent, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Compression) {
        status = android::gralloc4::encodeCompression(android::gralloc4::Compression_None,
                                                      &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Interlaced) {
        status = android::gralloc4::encodeInterlaced(android::gralloc4::Interlaced_None,
                                                     &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_ChromaSiting) {
        status = android::gralloc4::encodeChromaSiting(android::gralloc4::ChromaSiting_None,
                                                       &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Crop) {
        uint32_t drmFormat;
        if (getResolvedDrmFormat(descriptor.format, descriptor.usage, &drmFormat)) {
            hidlCb(Error::BAD_VALUE, encodedMetadata);
            return Void();
        }

        size_t numPlanes = drv_num_planes_from_format(drmFormat);

        std::vector<aidl::android::hardware::graphics::common::Rect> crops;
        for (size_t plane = 0; plane < numPlanes; plane++) {
            aidl::android::hardware::graphics::common::Rect crop;
            crop.left = 0;
            crop.top = 0;
            crop.right = descriptor.width;
            crop.bottom = descriptor.height;
            crops.push_back(crop);
        }
        status = android::gralloc4::encodeCrop(crops, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Dataspace) {
        status = android::gralloc4::encodeDataspace(Dataspace::UNKNOWN, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_BlendMode) {
        status = android::gralloc4::encodeBlendMode(BlendMode::INVALID, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Smpte2086) {
        status = android::gralloc4::encodeSmpte2086(std::nullopt, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Cta861_3) {
        status = android::gralloc4::encodeCta861_3(std::nullopt, &encodedMetadata);
    } else if (metadataType == android::gralloc4::MetadataType_Smpte2094_40) {
        status = android::gralloc4::encodeSmpte2094_40(std::nullopt, &encodedMetadata);
    } else {
        hidlCb(Error::UNSUPPORTED, encodedMetadata);
        return Void();
    }

    if (status != android::NO_ERROR) {
        hidlCb(Error::NO_RESOURCES, encodedMetadata);
        return Void();
    }

    hidlCb(Error::NONE, encodedMetadata);
    return Void();
}

Return<void> NxpMapper::listSupportedMetadataTypes(listSupportedMetadataTypes_cb hidlCb) {
    hidl_vec<MetadataTypeDescription> supported;

    if (!mDriver) {
        ALOGE("%s Driver is uninitialized.", __func__);
        hidlCb(Error::NO_RESOURCES, supported);
        return Void();
    }

    supported = hidl_vec<IMapper::MetadataTypeDescription>({
            {
                    android::gralloc4::MetadataType_BufferId,
                    "",
                    /*isGettable=*/true,
                    /*isSettable=*/false,
            },
            {
                    android::gralloc4::MetadataType_Name,
                    "",
                    /*isGettable=*/true,
                    /*isSettable=*/false,
            },
            {
                    android::gralloc4::MetadataType_Width,
                    "",
                    /*isGettable=*/true,
                    /*isSettable=*/false,
            },
            {
                    android::gralloc4::MetadataType_Height,
                    "",
                    /*isGettable=*/true,
                    /*isSettable=*/false,
            },
            {
                    android::gralloc4::MetadataType_LayerCount,
                    "",
                    /*isGettable=*/true,
                    /*isSettable=*/false,
            },
            {
                    android::gralloc4::MetadataType_PixelFormatRequested,
                    "",
                    /*isGettable=*/true,
                    /*isSettable=*/false,
            },
            {
                    android::gralloc4::MetadataType_PixelFormatFourCC,
                    "",
                    /*isGettable=*/true,
                    /*isSettable=*/false,
            },
            {
                    android::gralloc4::MetadataType_PixelFormatModifier,
                    "",
                    /*isGettable=*/true,
                    /*isSettable=*/false,
            },
            {
                    android::gralloc4::MetadataType_Usage,
                    "",
                    /*isGettable=*/true,
                    /*isSettable=*/false,
            },
            {
                    android::gralloc4::MetadataType_AllocationSize,
                    "",
                    /*isGettable=*/true,
                    /*isSettable=*/false,
            },
            {
                    android::gralloc4::MetadataType_ProtectedContent,
                    "",
                    /*isGettable=*/true,
                    /*isSettable=*/false,
            },
            {
                    android::gralloc4::MetadataType_Compression,
                    "",
                    /*isGettable=*/true,
                    /*isSettable=*/false,
            },
            {
                    android::gralloc4::MetadataType_Interlaced,
                    "",
                    /*isGettable=*/true,
                    /*isSettable=*/false,
            },
            {
                    android::gralloc4::MetadataType_ChromaSiting,
                    "",
                    /*isGettable=*/true,
                    /*isSettable=*/false,
            },
            {
                    android::gralloc4::MetadataType_PlaneLayouts,
                    "",
                    /*isGettable=*/true,
                    /*isSettable=*/false,
            },
            {
                    android::gralloc4::MetadataType_Dataspace,
                    "",
                    /*isGettable=*/true,
                    /*isSettable=*/true,
            },
            {
                    android::gralloc4::MetadataType_BlendMode,
                    "",
                    /*isGettable=*/true,
                    /*isSettable=*/true,
            },
            {
                    android::gralloc4::MetadataType_Smpte2086,
                    "",
                    /*isGettable=*/true,
                    /*isSettable=*/true,
            },
            {
                    android::gralloc4::MetadataType_Cta861_3,
                    "",
                    /*isGettable=*/true,
                    /*isSettable=*/true,
            },
            {
                    android::gralloc4::MetadataType_Smpte2094_40,
                    "",
                    /*isGettable=*/true,
                    /*isSettable=*/false,
            },
    });

    hidlCb(Error::NONE, supported);
    return Void();
}

Return<void> NxpMapper::dumpBuffer(void* rawHandle, dumpBuffer_cb hidlCb) {
    BufferDump bufferDump;

    if (!mDriver) {
        ALOGE("%s Driver is uninitialized.", __func__);
        hidlCb(Error::NO_RESOURCES, bufferDump);
        return Void();
    }

    const native_handle_t* bufferHandle = getConstImportedBuffer(rawHandle);
    if (!bufferHandle) {
        ALOGE("%s Empty handle.", __func__);
        hidlCb(Error::BAD_BUFFER, bufferDump);
        return Void();
    }

    gralloc_handle_t memHandle = gralloc_convert_handle(bufferHandle);
    if (!memHandle) {
        ALOGE("%s Invalid handle.", __func__);
        hidlCb(Error::BAD_BUFFER, bufferDump);
        return Void();
    }

    return dumpBuffer(memHandle, hidlCb);
}

Return<void> NxpMapper::dumpBuffer(gralloc_handle_t memHandle,
                                            dumpBuffer_cb hidlCb) {
    BufferDump bufferDump;

    if (!mDriver) {
        ALOGE("%s Driver is uninitialized.", __func__);
        hidlCb(Error::NO_RESOURCES, bufferDump);
        return Void();
    }

    if (!memHandle) {
        ALOGE("%s Invalid handle.", __func__);
        hidlCb(Error::BAD_BUFFER, bufferDump);
        return Void();
    }

    std::vector<MetadataDump> metadataDumps;

    MetadataType metadataType = android::gralloc4::MetadataType_BufferId;
    auto metadata_get_callback = [&](Error, hidl_vec<uint8_t> metadata) {
        MetadataDump metadataDump;
        metadataDump.metadataType = metadataType;
        metadataDump.metadata = metadata;
        metadataDumps.push_back(metadataDump);
    };

    metadataType = android::gralloc4::MetadataType_BufferId;
    get(memHandle, metadataType, metadata_get_callback);

    metadataType = android::gralloc4::MetadataType_Name;
    get(memHandle, metadataType, metadata_get_callback);

    metadataType = android::gralloc4::MetadataType_Width;
    get(memHandle, metadataType, metadata_get_callback);

    metadataType = android::gralloc4::MetadataType_Height;
    get(memHandle, metadataType, metadata_get_callback);

    metadataType = android::gralloc4::MetadataType_LayerCount;
    get(memHandle, metadataType, metadata_get_callback);

    metadataType = android::gralloc4::MetadataType_PixelFormatRequested;
    get(memHandle, metadataType, metadata_get_callback);

    metadataType = android::gralloc4::MetadataType_PixelFormatFourCC;
    get(memHandle, metadataType, metadata_get_callback);

    metadataType = android::gralloc4::MetadataType_PixelFormatModifier;
    get(memHandle, metadataType, metadata_get_callback);

    metadataType = android::gralloc4::MetadataType_Usage;
    get(memHandle, metadataType, metadata_get_callback);

    metadataType = android::gralloc4::MetadataType_AllocationSize;
    get(memHandle, metadataType, metadata_get_callback);

    metadataType = android::gralloc4::MetadataType_ProtectedContent;
    get(memHandle, metadataType, metadata_get_callback);

    metadataType = android::gralloc4::MetadataType_Compression;
    get(memHandle, metadataType, metadata_get_callback);

    metadataType = android::gralloc4::MetadataType_Interlaced;
    get(memHandle, metadataType, metadata_get_callback);

    metadataType = android::gralloc4::MetadataType_ChromaSiting;
    get(memHandle, metadataType, metadata_get_callback);

    metadataType = android::gralloc4::MetadataType_PlaneLayouts;
    get(memHandle, metadataType, metadata_get_callback);

    metadataType = android::gralloc4::MetadataType_Dataspace;
    get(memHandle, metadataType, metadata_get_callback);

    metadataType = android::gralloc4::MetadataType_BlendMode;
    get(memHandle, metadataType, metadata_get_callback);

    bufferDump.metadataDump = metadataDumps;
    hidlCb(Error::NONE, bufferDump);
    return Void();
}

Return<void> NxpMapper::dumpBuffers(dumpBuffers_cb hidlCb) {
    std::vector<BufferDump> bufferDumps;

    if (!mDriver) {
        ALOGE("%s Driver is uninitialized.", __func__);
        hidlCb(Error::NO_RESOURCES, bufferDumps);
        return Void();
    }

    Error error = Error::NONE;

    auto handleCallback = [&](gralloc_handle_t memHandle) {
        auto dumpBufferCallback = [&](Error err, BufferDump bufferDump) {
            error = err;
            if (error == Error::NONE) {
                bufferDumps.push_back(bufferDump);
            }
        };

        dumpBuffer(memHandle, dumpBufferCallback);
    };

    forEachGrallocHandle(handleCallback);

    hidlCb(error, bufferDumps);
    return Void();
}

Error NxpMapper::getReservedRegionArea(gralloc_handle_t memHandle,
                                                ReservedRegionArea area, void** outAddr,
                                                uint64_t* outSize) {
    if (!mDriver) {
        ALOGE("Failed to getReservedRegionArea. Driver is uninitialized.");
        return Error::NO_RESOURCES;
    }

    if (!memHandle) {
        ALOGE("Failed to getReservedRegionArea. Invalid buffer.");
        return Error::BAD_BUFFER;
    }

    int ret = mDriver->get_reserved_region(memHandle,outAddr, outSize);
    if (ret) {
        ALOGE("Failed to getReservedRegionArea.");
        *outAddr = nullptr;
        *outSize = 0;
        return Error::NO_RESOURCES;
    }

    switch (area) {
        case ReservedRegionArea::MAPPER4_METADATA: {
            // gralloc_metadata resides at the beginning reserved region.
            *outSize = sizeof(gralloc_metadata);
            break;
        }
        case ReservedRegionArea::USER_METADATA: {
            // User metadata resides after the gralloc_metadata.
            *outAddr = reinterpret_cast<void*>(reinterpret_cast<char*>(*outAddr) +
                                               sizeof(gralloc_metadata));
            *outSize = *outSize - sizeof(gralloc_metadata);
            break;
        }
    }

    return Error::NONE;
}

Error NxpMapper::getMetadata(gralloc_handle_t memHandle,
                                      const gralloc_metadata** outMetadata) {
    void* addr = nullptr;
    uint64_t size;

    Error error =
            getReservedRegionArea(memHandle, ReservedRegionArea::MAPPER4_METADATA, &addr, &size);
    if (error != Error::NONE) {
        return error;
    }

    *outMetadata = reinterpret_cast<const gralloc_metadata*>(addr);
    return Error::NONE;
}

Error NxpMapper::getMutableMetadata(gralloc_handle_t memHandle,
                                             gralloc_metadata** outMetadata) {
    void* addr = nullptr;
    uint64_t size;

    Error error =
            getReservedRegionArea(memHandle, ReservedRegionArea::MAPPER4_METADATA, &addr, &size);
    if (error != Error::NONE) {
        return error;
    }

    *outMetadata = reinterpret_cast<gralloc_metadata*>(addr);
    return Error::NONE;
}

Return<void> NxpMapper::getReservedRegion(void* rawHandle, getReservedRegion_cb hidlCb) {
    if (!mDriver) {
        ALOGE("%s Driver is uninitialized.", __func__);
        hidlCb(Error::NO_RESOURCES, nullptr, 0);
        return Void();
    }

    const native_handle_t* bufferHandle = getConstImportedBuffer(rawHandle);
    if (!bufferHandle) {
        ALOGE("%s Empty handle.", __func__);
        hidlCb(Error::BAD_BUFFER, nullptr, 0);
        return Void();
    }

    gralloc_handle_t memHandle = gralloc_convert_handle(bufferHandle);
    if (!memHandle) {
        ALOGE("%s Invalid handle.", __func__);
        hidlCb(Error::BAD_BUFFER, nullptr, 0);
        return Void();
    }

    void* reservedRegionAddr = nullptr;
    uint64_t reservedRegionSize = 0;

    Error error = getReservedRegionArea(memHandle, ReservedRegionArea::USER_METADATA, &reservedRegionAddr, &reservedRegionSize);

    if (error != Error::NONE) {
        ALOGE("Failed to getReservedRegion.");
        hidlCb(Error::BAD_BUFFER, nullptr, 0);
        return Void();
    }

    hidlCb(Error::NONE, reservedRegionAddr, reservedRegionSize);
    return Void();
}

android::hardware::graphics::mapper::V4_0::IMapper* HIDL_FETCH_IMapper(const char* /*name*/) {
    return static_cast<android::hardware::graphics::mapper::V4_0::IMapper*>(new NxpMapper);
}
