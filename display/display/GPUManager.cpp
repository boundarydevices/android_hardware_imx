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

#include <cutils/log.h>
#include "GPUManager.h"
#include <system/window.h>
#include <dlfcn.h>

namespace fsl {

#define GPU_MODULE_ID "gralloc_viv"

#if defined(__LP64__)
#define LIB_PATH1 "/system/lib64"
#define LIB_PATH2 "/vendor/lib64"
#else
#define LIB_PATH1 "/system/lib"
#define LIB_PATH2 "/vendor/lib"
#endif

#define GPUHELPER "libgpuhelper.so"

//-------------------------------------------
HelperShadow::HelperShadow(struct Memory* handle, bool own,
           helperFunc free, helperFunc unregister)
  : MemoryShadow(own), mHandle(handle), mHelperFree(free), mHelperUnregister(unregister)
{
}

HelperShadow::~HelperShadow()
{
    if (mHelperFree != NULL && mHelperUnregister != NULL && mHandle != NULL) {
        if (mOwner) {
            mHelperFree(mHandle);
        }
        else {
            mHelperUnregister(mHandle);
        }
    }
}

struct Memory* HelperShadow::handle()
{
    return mHandle;
}

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
        ALOGV("gpu gralloc module open failed!");
        return;
    }

    ret = gralloc_open((const hw_module_t*)mModule, &mAlloc);
    if (ret != 0 || !mAlloc) {
        ALOGV("gpu gralloc device open failed!");
        return;
    }

    mIonManager = new IonManager();

    char path[PATH_MAX] = {0};
    getModule(path, GPUHELPER);
    void* handle = dlopen(path, RTLD_NOW);
    if (handle == NULL) {
        ALOGV("no %s found", path);
        mHelperAlloc = NULL;
        mHelperFree = NULL;
        mHelperLock = NULL;
        mHelperUnlock = NULL;
        mHelperRegister = NULL;
        mHelperUnregister = NULL;
    }
    else {
        mHelperAlloc = (helperAlloc)dlsym(handle, "graphic_buffer_alloc");
        mHelperFree = (helperFunc)dlsym(handle, "graphic_buffer_free");
        mHelperLock = (helperFunc)dlsym(handle, "graphic_buffer_lock");
        mHelperUnlock = (helperFunc)dlsym(handle, "graphic_buffer_unlock");
        mHelperRegister = (helperFunc)dlsym(handle, "graphic_buffer_register");
        mHelperUnregister = (helperFunc)dlsym(handle, "graphic_buffer_unregister");
    }
    ALOGV("open gpu gralloc module success!");
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

bool GPUManager::useHelper(int format, int usage)
{
    bool helper = true;
    /*
     * RGB format and without video encoder flag go to VIV Gralloc.
     * Only READ/WRITE OFTEN flag go to VIV Gralloc,
     * which support allocate non physical continue memory.
     */
    if (((format >= FORMAT_RGBA8888 && format <= FORMAT_BGRA8888) &&
          !(usage & USAGE_HW_VIDEO_ENCODER)) ||
          (usage == (USAGE_SW_READ_OFTEN | USAGE_SW_WRITE_OFTEN))) {
        helper = false;
    }

    return helper;
}

void GPUManager::getModule(char *path, const char *name)
{
    snprintf(path, PATH_MAX, "%s/%s",
                          LIB_PATH1, name);
    if (access(path, R_OK) == 0)
        return;
    snprintf(path, PATH_MAX, "%s/%s",
                          LIB_PATH2, name);
    if (access(path, R_OK) == 0)
        return;
    return;
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
    if (desc.mFlag & FLAGS_FRAMEBUFFER) {
        return mIonManager->allocMemory(desc, out);
    }

    int ret = desc.checkFormat();
    if (ret != 0 || desc.mSize == 0) {
        ALOGE("%s check format failed", __func__);
        return -EINVAL;
    }

    Memory* memory = NULL;
    if (useHelper(desc.mFslFormat, desc.mProduceUsage) && mHelperAlloc != NULL) {
        ret = mHelperAlloc(desc.mWidth, desc.mHeight, desc.mFormat,
                 (int)desc.mProduceUsage, desc.mStride, desc.mSize, (void**)&memory);
    }
    else {
        ret = mAlloc->alloc(mAlloc, desc.mWidth, desc.mHeight, desc.mFormat,
                 (int)desc.mProduceUsage, (buffer_handle_t *)&memory, &desc.mStride);
    }
    if (ret != 0 || memory == NULL) {
        ALOGE("%s buffer alloc failed", __func__);
        return -EINVAL;
    }

    memory->fslFormat = desc.mFslFormat;
    if (desc.mFlag & FLAGS_FRAMEBUFFER) {
        memory->flags |= FLAGS_FRAMEBUFFER | FLAGS_ALLOCATION_GPU;
    }

    MemoryShadow* shadow = NULL;
    if (useHelper(desc.mFslFormat, desc.mProduceUsage) && mHelperAlloc != NULL) {
        shadow = new HelperShadow(memory, true, mHelperFree, mHelperUnregister);
    }
    else {
        shadow = new GPUShadow(memory, true, mAlloc, mModule);
    }
    memory->shadow = (uintptr_t)shadow;
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

    if (handle->flags & FLAGS_FRAMEBUFFER) {
        return mIonManager->retainMemory(handle);
    }

    MemoryShadow* shadow = (MemoryShadow*)(uintptr_t)handle->shadow;
    if (handle->pid != getpid()) {
        int ret = 0;
        if (useHelper(handle->fslFormat, handle->usage) && mHelperRegister != NULL) {
            ret = mHelperRegister(handle);
        }
        else {
            ret = mModule->registerBuffer(mModule, handle);
        }
        if (ret != 0) {
            ALOGE("%s register failed", __func__);
            return -EINVAL;
        }

        if (useHelper(handle->fslFormat, handle->usage) && mHelperRegister != NULL) {
            shadow = new HelperShadow(handle, false, mHelperFree, mHelperUnregister);
        }
        else {
            shadow = new GPUShadow(handle, false, mAlloc, mModule);
        }
        handle->shadow = (uintptr_t)shadow;
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

    if (handle->flags & FLAGS_FRAMEBUFFER) {
        return mIonManager->lock(handle, usage, l, t, w, h, vaddr);
    }

    int ret = 0;
    ret = verifyMemory(handle);
    if (ret != 0) {
        ALOGE("%s verify memory failed", __func__);
        return -EINVAL;
    }

    if (useHelper(handle->fslFormat, handle->usage) && mHelperLock != NULL) {
        ret = mHelperLock(handle);
    }
    else {
        ret = mModule->lock(mModule, handle, usage, l, t, w, h, vaddr);
    }
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

    if (handle->flags & FLAGS_FRAMEBUFFER) {
        return mIonManager->lockYCbCr(handle, usage, l, t, w, h, ycbcr);
    }

    if (useHelper(handle->fslFormat, handle->usage) && mHelperLock != NULL) {
        mHelperLock(handle);
        return MemoryManager::lockYCbCr(handle, usage, l, t, w, h, ycbcr);
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

    if (handle->flags & FLAGS_FRAMEBUFFER) {
        return mIonManager->unlock(handle);
    }

    if (useHelper(handle->fslFormat, handle->usage) && mHelperUnlock != NULL) {
        ret = mHelperUnlock(handle);
    }
    else {
        ret = mModule->unlock(mModule, handle);
    }
    if (ret != 0) {
        ALOGE("%s unlock failed", __func__);
        return -EINVAL;
    }

    return 0;
}

}
