/*
 * Copyright (C) 2013-2016 Freescale Semiconductor, Inc.
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

#include <cutils/log.h>
#include "GPUManager.h"
#include <system/window.h>

namespace fsl {

#define GPU_MODULE_ID "gralloc_viv"

//-------------------------------------------
GPUShadow::GPUShadow(struct Memory* handle, bool own,
           alloc_device_t* alloc, gralloc_module_t* module)
  : MemoryShadow(own), mHandle(handle), mAlloc(alloc), mModule(module)
{
}

GPUShadow::~GPUShadow()
{
    if (mAlloc != NULL && mModule != NULL && mHandle != NULL) {
        if (mOwner) {
            mAlloc->free(mAlloc, mHandle);
        }
        else {
            mModule->unregisterBuffer(mModule, mHandle);
        }
    }
}

struct Memory* GPUShadow::handle()
{
    return mHandle;
}

//-------------------------------------------
GPUManager::GPUManager()
{
    mModule = NULL;
    mAlloc = NULL;
    int ret = hw_get_module(GPU_MODULE_ID, (const hw_module_t**)&mModule);
    if (ret != 0) {
        ALOGE("gpu gralloc module open failed!");
        return;
    }

    ret = gralloc_open((const hw_module_t*)mModule, &mAlloc);
    if (ret != 0 || !mAlloc) {
        ALOGE("gpu gralloc device open failed!");
        return;
    }

    ALOGI("open gpu gralloc module success!");
}

GPUManager::~GPUManager()
{
    if (mAlloc != NULL) {
        mAlloc->common.close((struct hw_device_t *)mAlloc);
    }
}

bool GPUManager::isValid()
{
    return ((mModule != NULL) && mAlloc != NULL);
}

int GPUManager::allocMemory(MemoryDesc& desc, Memory** out)
{
    if (mAlloc == NULL) {
        ALOGE("%s invalid gpu device", __func__);
        return -EINVAL;
    }

    if (out == NULL) {
        ALOGE("%s invalid parameters", __func__);
        return -EINVAL;
    }

    desc.mFlag |= FLAGS_ALLOCATION_GPU;
    int ret = desc.checkFormat();
    if (ret != 0 || desc.mSize == 0) {
        ALOGE("%s check format failed", __func__);
        return -EINVAL;
    }

    Memory* memory = NULL;
    ret = mAlloc->alloc(mAlloc, desc.mWidth, desc.mHeight, desc.mFormat,
                 desc.mProduceUsage, (buffer_handle_t *)&memory, &desc.mStride);
    if (ret != 0 || memory == NULL) {
        ALOGE("%s buffer alloc failed", __func__);
        return -EINVAL;
    }

    memory->fslFormat = desc.mFslFormat;
    if (desc.mFlag & FLAGS_FRAMEBUFFER) {
        memory->flags |= FLAGS_FRAMEBUFFER | FLAGS_ALLOCATION_GPU;
    }

    MemoryShadow* shadow = new GPUShadow(memory, true, mAlloc, mModule);
    memory->shadow = (int)(intptr_t)shadow;
    *out = memory;

    return 0;
}

int GPUManager::retainMemory(Memory* handle)
{
    if (mAlloc == NULL) {
        ALOGE("%s invalid gpu device", __func__);
        return -EINVAL;
    }

    if (handle == NULL || !handle->isValid()) {
        ALOGE("%s invalid handle", __func__);
        return -EINVAL;
    }

    MemoryShadow* shadow = (MemoryShadow*)(intptr_t)handle->shadow;
    if (handle->pid != getpid()) {
        int ret = mModule->registerBuffer(mModule, handle);
        if (ret != 0) {
            ALOGE("%s register failed", __func__);
            return -EINVAL;
        }

        shadow = new GPUShadow(handle, false, mAlloc, mModule);
        handle->shadow = (int)(intptr_t)shadow;
        handle->pid = getpid();
    }
    else {
        if (shadow == NULL) {
            ALOGE("%s buffer handle invalid", __func__);
            return -EINVAL;
        }

        shadow->incRef();
    }

    return 0;
}

int GPUManager::lock(Memory* handle, int usage,
        int l, int t, int w, int h, void** vaddr)
{
    if (mAlloc == NULL) {
        ALOGE("%s invalid gpu device", __func__);
        return -EINVAL;
    }

    int ret = 0;
    ret = verifyMemory(handle);
    if (ret != 0) {
        ALOGE("%s verify memory failed", __func__);
        return -EINVAL;
    }

    ret = mModule->lock(mModule, handle, usage, l, t, w, h, vaddr);
    if (ret != 0 || handle->base == 0) {
        ALOGE("%s lock failed", __func__);
        return -EINVAL;
    }
    *vaddr = (void *)handle->base;

    return 0;
}

int GPUManager::lockYCbCr(Memory* handle, int usage,
        int l, int t, int w, int h,
        android_ycbcr* ycbcr)
{
    if (mAlloc == NULL) {
        ALOGE("%s invalid gpu device", __func__);
        return -EINVAL;
    }

    int ret = 0;
    ret = verifyMemory(handle);
    if (ret != 0) {
        ALOGE("%s verify memory failed", __func__);
        return -EINVAL;
    }

    return mModule->lock_ycbcr(mModule, handle, usage, l, t, w, h, ycbcr);
}

int GPUManager::unlock(Memory* handle)
{
    if (mAlloc == NULL) {
        ALOGE("%s invalid gpu device", __func__);
        return -EINVAL;
    }

    int ret = 0;
    ret = verifyMemory(handle);
    if (ret != 0) {
        ALOGE("%s verify memory failed", __func__);
        return -EINVAL;
    }

    ret = mModule->unlock(mModule, handle);
    if (ret != 0) {
        ALOGE("%s unlock failed", __func__);
        return -EINVAL;
    }

    return 0;
}

}
