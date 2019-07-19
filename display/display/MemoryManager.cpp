/*
 * Copyright (C) 2013-2016 Freescale Semiconductor, Inc.
 * Copyright 2017 NXP.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sys/mman.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <cutils/ashmem.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "MemoryManager.h"

namespace fsl {

#define GPU_MODULE_ID "gralloc_viv"

MemoryManager* MemoryManager::sInstance(0);
Mutex MemoryManager::sLock(Mutex::PRIVATE);

MemoryManager* MemoryManager::getInstance()
{
    Mutex::Autolock _l(sLock);
    if (sInstance != NULL) {
        return sInstance;
    }

    sInstance = new MemoryManager();

    return sInstance;
}

MemoryManager::MemoryManager()
{
    mIonManager = new IonManager();

    mGPUModule = NULL;
    mGPUAlloc = NULL;
    ALOGI("open gpu gralloc module!");
    if (hw_get_module(GPU_MODULE_ID, (const hw_module_t**)&mGPUModule) == 0) {
        int status = gralloc_open((const hw_module_t*)mGPUModule, &mGPUAlloc);
        if(status || !mGPUAlloc){
            ALOGI("no gpu gralloc device!");
        }
    }
    else {
        ALOGI("no gpu gralloc module!");
    }
}

MemoryManager::~MemoryManager()
{
    if (mIonManager != NULL) {
        delete mIonManager;
    }
    if (mGPUAlloc != NULL) {
        mGPUAlloc->common.close((struct hw_device_t *)mGPUAlloc);
    }
}

bool MemoryManager::isDrmAlloc(int flags, int format, int usage)
{
    bool canHandle = true;

    /* The following conditions decide allocator.
     * 1) framebuffer (except 8mscale) should use ION.
     * 2) Hantro VPU needs special size should use ION.
     * 3) secure memory should use ION.
     * 4) Dim buffer should use ION.
     * 5) VPU post process buffer (RGB565, USAGE_PADDING_BUFFER) use ION.
     * 6) encoder memory should use ION.
     * 7) all YUV format use ION.
     * 8) other conditions can use DRM Gralloc.
    */
#ifdef FRAMEBUFFER_COMPRESSION
    if (flags & (FLAGS_SECURE | FLAGS_DIMBUFFER)) {
#else
    if (flags & (FLAGS_FRAMEBUFFER | FLAGS_SECURE | FLAGS_DIMBUFFER)) {
#endif
        canHandle = false;
    }
    else if (mGPUAlloc == NULL) {
        canHandle = false;
    }
    else if ((format == FORMAT_RGB565) && (usage & USAGE_PADDING_BUFFER)) {
        canHandle = false;
    }
    else if ((format == FORMAT_NV12) || (format == FORMAT_NV21) ||
        (format == FORMAT_NV16) || (format == FORMAT_YUYV) ||
        (format == FORMAT_YV12) || (format == FORMAT_I420) ||
        (format == FORMAT_NV12_TILED) ||
        format == FORMAT_NV12_G1_TILED || format == FORMAT_NV12_G2_TILED ||
        format == FORMAT_NV12_G2_TILED_COMPRESSED || format == FORMAT_P010 ||
        format == FORMAT_P010_TILED || format == FORMAT_P010_TILED_COMPRESSED) {
        canHandle = false;
    }
    else if (usage & USAGE_HW_VIDEO_ENCODER) {
        canHandle = false;
    }

    return canHandle;
}

int MemoryManager::allocMemory(MemoryDesc& desc, Memory** out)
{
    Memory *handle = NULL;
    int ret = 0;

#ifdef CFG_SECURE_DATA_PATH
    if (desc.mProduceUsage & USAGE_PROTECTED) {
        desc.mFlag |= FLAGS_SECURE;
    }
#endif
    if (isDrmAlloc(desc.mFlag, desc.mFslFormat, desc.mProduceUsage)) {
        ret = mGPUAlloc->alloc(mGPUAlloc, desc.mWidth, desc.mHeight,
                desc.mFormat, (int)desc.mProduceUsage,
                (buffer_handle_t *)&handle, &desc.mStride);
        if (ret == 0 && handle != NULL) {
            handle->fslFormat = desc.mFslFormat;
#ifdef FRAMEBUFFER_COMPRESSION
            if (desc.mFlag & FLAGS_FRAMEBUFFER) {
                mIonManager->getPhys(handle);
                handle->flags = desc.mFlag;
            }
#endif
        }
        allocMetaData(handle);
        *out = handle;
        return ret;
    }

    ret = mIonManager->allocMemory(desc, &handle);
    if (ret != 0 || handle == NULL) {
        ALOGE("%s alloc ion memory failed", __func__);
        return -EINVAL;
    }

    allocMetaData(handle);
    retainMemory(handle);
    *out = handle;

    return 0;
}

int MemoryManager::retainMemory(Memory* handle)
{
    if (handle == NULL || !handle->isValid()) {
        ALOGE("%s invalid handle", __func__);
        return -EINVAL;
    }

    if (isDrmAlloc(handle->flags, handle->fslFormat, handle->usage)) {
        return mGPUModule->registerBuffer(mGPUModule, handle);
    }

    mIonManager->getVaddrs(handle);
    handle->fbId = 0;
    handle->fbHandle = 0;

    return 0;
}

int MemoryManager::releaseMemory(Memory* handle)
{
    int ret;
    if (handle == NULL || !handle->isValid()) {
        ALOGE("%s invalid handle", __func__);
        return -EINVAL;
    }

    if (isDrmAlloc(handle->flags, handle->fslFormat, handle->usage)) {
        if (handle->fd_meta > 0) {
            ret = close(handle->fd_meta);
            handle->fd_meta = 0;
            if(ret != 0){
                ALOGE("%s: close DRM allocated fd_meta failed as errno %s", __func__,
                        strerror(errno));
            }
        }
        return mGPUAlloc->free(mGPUAlloc, handle);
    }

    /* kmsFd, fbHandle and fbId are created in KmsDisplay.
     * It is hard to put free memory code to KmsDisplay.
    */
    if (handle->kmsFd > 0) {
        if (handle->fbId != 0) {
            drmModeRmFB(handle->kmsFd, handle->fbId);
        }
        if (handle->fbHandle != 0) {
            struct drm_gem_close gem_close;
            memset(&gem_close, 0, sizeof(gem_close));
            gem_close.handle = handle->fbHandle;
            drmIoctl(handle->kmsFd, DRM_IOCTL_GEM_CLOSE, &gem_close);
        }
    }
    handle->fbId = 0;
    handle->fbHandle = 0;

    if (handle->base != 0) {
        munmap((void*)handle->base, handle->size);
    }

    if (handle->fd > 0) {
        ret = close(handle->fd);
        handle->fd = 0;
        if(ret != 0){
            ALOGE("%s: close fd failed as errno %s", __func__,
                strerror(errno));
        }
    }

    if (mMetaMap.indexOfKey(handle) >= 0) {
        uint64_t addr = mMetaMap.valueFor(handle);
        munmap((void*)addr, sizeof(MetaData));
        mMetaMap.removeItem(handle);
    }

    if (handle->fd_meta > 0) {
        ret = close(handle->fd_meta);
        handle->fd_meta = 0;
        if(ret != 0){
            ALOGE("%s: close fd_meta failed as errno %s", __func__,
                strerror(errno));
        }
    }

    delete handle;

    return 0;
}

int MemoryManager::lock(Memory* handle, int usage,
        int l, int t, int w, int h, void** vaddr)
{
    if (handle == NULL || !handle->isValid()) {
        ALOGE("%s invalid handle", __func__);
        return -EINVAL;
    }

    if (isDrmAlloc(handle->flags, handle->fslFormat, handle->usage)) {
        return mGPUModule->lock(mGPUModule, handle, usage,
                    l, t, w, h, vaddr);
    }

    return mIonManager->lock(handle, usage, l, t, w, h, vaddr);
}

int MemoryManager::lockYCbCr(Memory* handle, int usage,
            int l, int t, int w, int h,
            android_ycbcr* ycbcr)
{
    if (handle == NULL || !handle->isValid() || ycbcr == NULL) {
        ALOGE("%s invalid handle", __func__);
        return -EINVAL;
    }

    if (isDrmAlloc(handle->flags, handle->fslFormat, handle->usage)) {
        return mGPUModule->lock_ycbcr(mGPUModule, handle, usage,
                    l, t, w, h, ycbcr);
    }

    int ret = mIonManager->lockYCbCr(handle, usage, l, t, w, h, ycbcr);
    if (ret != 0) {
        ALOGE("%s ion lock failed", __func__);
        return -EINVAL;
    }

    switch (handle->fslFormat) {
        case FORMAT_NV12:
            ycbcr->ystride = handle->stride;
            ycbcr->cstride = ycbcr->ystride;
            ycbcr->y = (void*)handle->base;
            ycbcr->cb = (void*)(handle->base + handle->stride*ALIGN_PIXEL_4(handle->height));
            ycbcr->cr = (void*)((uintptr_t)ycbcr->cb + 1);
            ycbcr->chroma_step = 2;
            break;

        case FORMAT_NV21:
            ycbcr->ystride = handle->stride;
            ycbcr->cstride = ycbcr->ystride;
            ycbcr->y = (void*)handle->base;
            ycbcr->cr = (void*)(handle->base + handle->stride*ALIGN_PIXEL_4(handle->height));
            ycbcr->cb = (void*)((uintptr_t)ycbcr->cr + 1);
            ycbcr->chroma_step = 2;
            break;

        case FORMAT_I420:
            ycbcr->ystride = handle->stride;
            ycbcr->cstride = ycbcr->ystride / 2;
            ycbcr->y = (void*)handle->base;
            ycbcr->cb = (void*)(handle->base + handle->stride*ALIGN_PIXEL_4(handle->height));
            ycbcr->cr = (void*)((uintptr_t)ycbcr->cb + ycbcr->cstride*ALIGN_PIXEL_4(handle->height)/2);
            ycbcr->chroma_step = 1;
            break;

        case FORMAT_YV12:
            ycbcr->ystride = handle->stride;
            ycbcr->cstride = ycbcr->ystride / 2;
            ycbcr->y = (void*)handle->base;
            ycbcr->cr = (void*)(handle->base + handle->stride*ALIGN_PIXEL_4(handle->height));
            ycbcr->cb = (void*)((uintptr_t)ycbcr->cr + ycbcr->cstride*ALIGN_PIXEL_4(handle->height)/2);
            ycbcr->chroma_step = 1;
            break;

        default:
            ALOGE("%s not support format:0x%x", __func__, handle->format);
            return -EINVAL;
    }

    return 0;
}

int MemoryManager::unlock(Memory* handle)
{
    if (handle == NULL || !handle->isValid()) {
        ALOGE("%s invalid handle", __func__);
        return -EINVAL;
    }

    if (isDrmAlloc(handle->flags, handle->fslFormat, handle->usage)) {
        return mGPUModule->unlock(mGPUModule, handle);
    }

    return mIonManager->unlock(handle);
}

int MemoryManager::allocMetaData(Memory* handle)
{
    if (handle == NULL || !handle->isValid()) {
        ALOGE("%s invalid handle", __func__);
        return -EINVAL;
    }

    size_t size = 2 * sizeof(MetaData);
    handle->fd_meta = ashmem_create_region("meta", size);
    return handle->fd_meta > 0;
}

MetaData *MemoryManager::getMetaData(Memory* handle)
{
    if (handle == NULL || !handle->isValid()) {
        ALOGE("%s invalid handle", __func__);
        return NULL;
    }

    if (handle->fd_meta <= 0) {
        return NULL;
    }

    if (mMetaMap.indexOfKey(handle) >= 0) {
        uint64_t addr = mMetaMap.valueFor(handle);
        return (MetaData *)addr;
    }

    void *addr = NULL;
    int fd = handle->fd_meta;
    size_t size = sizeof(MetaData);
    addr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        return NULL;
    }

    mMetaMap.add(handle, (uint64_t)addr);
    return (MetaData *)addr;
}

}
