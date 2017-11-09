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

int MemoryManager::allocMemory(MemoryDesc& desc, Memory** out)
{
    Memory *handle = NULL;
    int ret = 0;

    if (!(desc.mFlag & FLAGS_FRAMEBUFFER) && mGPUAlloc != NULL) {
        ret = mGPUAlloc->alloc(mGPUAlloc, desc.mWidth, desc.mHeight,
                desc.mFormat, (int)desc.mProduceUsage,
                (buffer_handle_t *)&handle, &desc.mStride);
        if (ret == 0 && handle != NULL) {
            handle->fslFormat = desc.mFslFormat;
        }
        *out = handle;
        return ret;
    }

    ret = mIonManager->allocMemory(desc, &handle);
    if (ret != 0 || handle == NULL) {
        ALOGE("%s alloc ion memory failed", __func__);
        return -EINVAL;
    }

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

    if (!(handle->flags & FLAGS_FRAMEBUFFER) && mGPUAlloc != NULL) {
        return mGPUModule->registerBuffer(mGPUModule, handle);
    }

    mIonManager->getVaddrs(handle);

    return 0;
}

int MemoryManager::releaseMemory(Memory* handle)
{
    if (handle == NULL || !handle->isValid()) {
        ALOGE("%s invalid handle", __func__);
        return -EINVAL;
    }

    if (!(handle->flags & FLAGS_FRAMEBUFFER) && mGPUAlloc != NULL) {
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

    if (handle->base != 0) {
        munmap((void*)handle->base, handle->size);
    }

    close(handle->fd);
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

    if (!(handle->flags & FLAGS_FRAMEBUFFER) && mGPUAlloc != NULL) {
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

    if (!(handle->flags & FLAGS_FRAMEBUFFER) && mGPUAlloc != NULL) {
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
            ycbcr->cb = (void*)(handle->base + handle->stride*handle->height);
            ycbcr->cr = (void*)((uintptr_t)ycbcr->cb + 1);
            ycbcr->chroma_step = 2;
            break;

        case FORMAT_NV21:
            ycbcr->ystride = handle->stride;
            ycbcr->cstride = ycbcr->ystride;
            ycbcr->y = (void*)handle->base;
            ycbcr->cr = (void*)(handle->base + handle->stride*handle->height);
            ycbcr->cb = (void*)((uintptr_t)ycbcr->cr + 1);
            ycbcr->chroma_step = 2;
            break;

        case FORMAT_I420:
            ycbcr->ystride = handle->stride;
            ycbcr->cstride = ycbcr->ystride / 2;
            ycbcr->y = (void*)handle->base;
            ycbcr->cb = (void*)(handle->base + handle->stride*handle->height);
            ycbcr->cr = (void*)((uintptr_t)ycbcr->cb + ycbcr->cstride*handle->height/2);
            ycbcr->chroma_step = 1;
            break;

        case FORMAT_YV12:
            ycbcr->ystride = handle->stride;
            ycbcr->cstride = ycbcr->ystride / 2;
            ycbcr->y = (void*)handle->base;
            ycbcr->cr = (void*)(handle->base + handle->stride*handle->height);
            ycbcr->cb = (void*)((uintptr_t)ycbcr->cr + ycbcr->cstride*handle->height/2);
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

    if (!(handle->flags & FLAGS_FRAMEBUFFER) && mGPUAlloc != NULL) {
        return mGPUModule->unlock(mGPUModule, handle);
    }

    return mIonManager->unlock(handle);
}

}
