/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <android/hardware/graphics/mapper/4.0/IMapper.h>

#include <unordered_set>

#include "gralloc_driver.h"
#include "gralloc_handle.h"
#include "gralloc_metadata.h"

class GrallocImportedBufferPool {
public:
    static GrallocImportedBufferPool& getInstance() {
        static GrallocImportedBufferPool* singleton = new GrallocImportedBufferPool;
        return *singleton;
    }

    void* add(native_handle_t* bufferHandle) {
        std::lock_guard<std::mutex> lock(mMutex);
        return mBufferHandles.insert(bufferHandle).second ? bufferHandle : nullptr;
    }

    native_handle_t* remove(void* buffer) {
        auto bufferHandle = static_cast<native_handle_t*>(buffer);

        std::lock_guard<std::mutex> lock(mMutex);
        return mBufferHandles.erase(bufferHandle) == 1 ? bufferHandle : nullptr;
    }

    native_handle_t* get(void* buffer) {
        auto bufferHandle = static_cast<native_handle_t*>(buffer);

        std::lock_guard<std::mutex> lock(mMutex);
        return mBufferHandles.count(bufferHandle) == 1 ? bufferHandle : nullptr;
    }

    const native_handle_t* getConst(void* buffer) {
        auto bufferHandle = static_cast<const native_handle_t*>(buffer);

        std::lock_guard<std::mutex> lock(mMutex);
        return mBufferHandles.count(bufferHandle) == 1 ? bufferHandle : nullptr;
    }

    void forEachHandle(const std::function<void(gralloc_handle_t)>& function) {
        std::lock_guard<std::mutex> lock(mMutex);
        for (const auto& hnd : mBufferHandles) {
            function(static_cast<gralloc_handle_t>(hnd));
        }
    }

private:
    std::mutex mMutex;
    std::unordered_set<const native_handle_t*> mBufferHandles;
};

class NxpMapper : public android::hardware::graphics::mapper::V4_0::IMapper {
public:
    NxpMapper();

    android::hardware::Return<void> createDescriptor(const BufferDescriptorInfo& description,
                                                     createDescriptor_cb hidlCb) override;

    android::hardware::Return<void> importBuffer(const android::hardware::hidl_handle& rawHandle,
                                                 importBuffer_cb hidlCb) override;

    android::hardware::Return<android::hardware::graphics::mapper::V4_0::Error> freeBuffer(
            void* rawHandle) override;

    android::hardware::Return<android::hardware::graphics::mapper::V4_0::Error> validateBufferSize(
            void* rawHandle, const BufferDescriptorInfo& descriptor, uint32_t stride) override;

    android::hardware::Return<void> getTransportSize(void* rawHandle,
                                                     getTransportSize_cb hidlCb) override;

    android::hardware::Return<void> lock(void* rawHandle, uint64_t cpuUsage,
                                         const Rect& accessRegion,
                                         const android::hardware::hidl_handle& acquireFence,
                                         lock_cb hidlCb) override;

    android::hardware::Return<void> unlock(void* rawHandle, unlock_cb hidlCb) override;

    android::hardware::Return<void> flushLockedBuffer(void* rawHandle,
                                                      flushLockedBuffer_cb hidlCb) override;

    android::hardware::Return<android::hardware::graphics::mapper::V4_0::Error> rereadLockedBuffer(
            void* rawHandle) override;

    android::hardware::Return<void> isSupported(const BufferDescriptorInfo& descriptor,
                                                isSupported_cb hidlCb) override;

    android::hardware::Return<void> get(void* rawHandle, const MetadataType& metadataType,
                                        get_cb hidlCb) override;

    android::hardware::Return<android::hardware::graphics::mapper::V4_0::Error> set(
            void* rawHandle, const MetadataType& metadataType,
            const android::hardware::hidl_vec<uint8_t>& metadata) override;

    android::hardware::Return<void> getFromBufferDescriptorInfo(
            const BufferDescriptorInfo& descriptor, const MetadataType& metadataType,
            getFromBufferDescriptorInfo_cb hidlCb) override;

    android::hardware::Return<void> listSupportedMetadataTypes(
            listSupportedMetadataTypes_cb hidlCb) override;

    android::hardware::Return<void> dumpBuffer(void* rawHandle, dumpBuffer_cb hidlCb) override;
    android::hardware::Return<void> dumpBuffers(dumpBuffers_cb hidlCb) override;

    android::hardware::Return<void> getReservedRegion(void* rawHandle,
                                                      getReservedRegion_cb hidlCb) override;

protected:
    void* addImportedBuffer(native_handle_t* bufferHandle) {
        return GrallocImportedBufferPool::getInstance().add(bufferHandle);
    }

    native_handle_t* removeImportedBuffer(void* buffer) {
        return GrallocImportedBufferPool::getInstance().remove(buffer);
    }

    native_handle_t* getImportedBuffer(void* buffer) {
        return GrallocImportedBufferPool::getInstance().get(buffer);
    }

    const native_handle_t* getConstImportedBuffer(void* buffer) {
        return GrallocImportedBufferPool::getInstance().getConst(buffer);
    }

    void forEachGrallocHandle(const std::function<void(gralloc_handle_t)>& function) {
        return GrallocImportedBufferPool::getInstance().forEachHandle(function);
    }

private:
    android::hardware::Return<void> get(gralloc_handle_t crosHandle,
                                        const MetadataType& metadataType, get_cb hidlCb);

    android::hardware::graphics::mapper::V4_0::Error set(
            gralloc_handle_t memHandle, const MetadataType& metadataType,
            const android::hardware::hidl_vec<uint8_t>& metadata);

    android::hardware::Return<void> dumpBuffer(gralloc_handle_t crosHandle, dumpBuffer_cb hidlCb);

    int getResolvedDrmFormat(android::hardware::graphics::common::V1_2::PixelFormat pixelFormat,
                             uint64_t bufferUsage, uint32_t* outDrmFormat);

    std::unique_ptr<gralloc_driver> mDriver;

    enum class ReservedRegionArea {
        /* gralloc_metadata */
        MAPPER4_METADATA,

        /* External user metadata */
        USER_METADATA,
    };

    android::hardware::graphics::mapper::V4_0::Error getReservedRegionArea(
            gralloc_handle_t memHandle, ReservedRegionArea area, void** outAddr, uint64_t* outSize);

    android::hardware::graphics::mapper::V4_0::Error getMetadata(
            gralloc_handle_t memHandle, const gralloc_metadata** outMetadata);

    android::hardware::graphics::mapper::V4_0::Error getMutableMetadata(
            gralloc_handle_t memHandle, gralloc_metadata** outMetadata);
};

extern "C" android::hardware::graphics::mapper::V4_0::IMapper* HIDL_FETCH_IMapper(const char* name);
