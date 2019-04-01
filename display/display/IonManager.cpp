/*
 * Copyright (C) 2013-2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2018 NXP.
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
#include "IonManager.h"

#define ION_DECODED_BUFFER_VPU_ALIGN 8

namespace fsl {

IonManager::IonManager()
    : mAllocator(NULL)
{
    mAllocator = IonAllocator::getInstance();
}

IonManager::~IonManager()
{
}

int IonManager::allocMemory(MemoryDesc& desc, Memory** out)
{
    if (out == NULL || mAllocator == NULL) {
        ALOGE("%s invalid parameters", __func__);
        return -EINVAL;
    }

    desc.mFlag |= FLAGS_ALLOCATION_ION;
    int ret = desc.checkFormat();
    if (ret != 0 || desc.mSize == 0) {
        ALOGE("%s check format failed", __func__);
        return -EINVAL;
    }

    unsigned char *ptr = NULL;
    int sharedFd = -1;
    Memory* memory = NULL;
    int align = ION_MEM_ALIGN;
    int flags = MFLAGS_CONTIGUOUS;

#ifdef CFG_SECURE_DATA_PATH
    if (desc.mFlag & FLAGS_SECURE)
    {
        align = ION_DECODED_BUFFER_VPU_ALIGN;
        flags = MFLAGS_SECURE;
    }
#endif

    if (desc.mProduceUsage & (USAGE_SW_READ_OFTEN | USAGE_SW_WRITE_OFTEN)) {
        flags |= MFLAGS_CACHEABLE;
    }

    sharedFd = mAllocator->allocMemory(desc.mSize, align, flags);
    if (sharedFd < 0) {
        ALOGE("allocator allocMemory failed");
        return -EINVAL;
    }

    memory = new Memory(&desc, sharedFd, -1);
    getPhys(memory);

    *out = memory;
    close(sharedFd);

    return 0;
}

int IonManager::getPhys(Memory* memory)
{
    if (mAllocator == NULL || memory == NULL || memory->fd < 0) {
        ALOGE("%s invalid parameters", __func__);
        return -EINVAL;
    }

    uint64_t phyAddr = 0;
    int ret = mAllocator->getPhys(memory->fd, memory->size, phyAddr);
    if (ret != 0) {
        ALOGE("allocator get phys failed");
        return -EINVAL;
    }

    memory->phys = phyAddr;
    return 0;
}

int IonManager::getVaddrs(Memory* memory)
{
    if (mAllocator == NULL || memory == NULL || memory->fd < 0) {
        ALOGE("%s invalid parameters", __func__);
        return -EINVAL;
    }

    uint64_t base = 0;
    int ret = mAllocator->getVaddrs(memory->fd, memory->size, base);
    if (ret != 0) {
        ALOGE("allocator get vaddrs failed");
        return -EINVAL;
    }

    memory->base = base;
    return 0;
}

int IonManager::flushCache(Memory* memory)
{
    if (mAllocator == NULL || memory == NULL || memory->fd < 0) {
        ALOGE("%s invalid parameters", __func__);
        return -EINVAL;
    }

    return mAllocator->flushCache(memory->fd);
}

int IonManager::lock(Memory* handle, int /*usage*/,
        int /*l*/, int /*t*/, int /*w*/, int /*h*/, void** vaddr)
{
    if (handle->base == 0) {
        getVaddrs(handle);
    }
    *vaddr = (void *)handle->base;

    return 0;
}

int IonManager::lockYCbCr(Memory* handle, int /*usage*/,
        int /*l*/, int /*t*/, int /*w*/, int /*h*/, android_ycbcr* /*ycbcr*/)
{
    if (handle->base == 0) {
        getVaddrs(handle);
    }

    return 0;
}

int IonManager::unlock(Memory* handle)
{
    if (handle->flags & FLAGS_CPU) {
        flushCache(handle);
    }

    return 0;
}

}
