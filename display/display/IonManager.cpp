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
#include "IonManager.h"
#include <ion/ion.h>
#include <linux/mxc_ion.h>
#include <ion_ext.h>

namespace fsl {

inline size_t roundUpToPageSize(size_t x) {
    return (x + (PAGE_SIZE-1)) & ~(PAGE_SIZE-1);
}

//--------------------------------------------------
IonShadow::IonShadow(int fd, struct Memory* handle, bool own)
  : MemoryShadow(own), mFd(dup(fd)), mHandle(handle)
{
    if (mHandle != NULL) {
        mHandle->base = 0;
    }
}

IonShadow::~IonShadow()
{
    if (mHandle != NULL) {
        if (mHandle->base != 0) {
            munmap((void*)mHandle->base, mHandle->size);
        }

        if (mOwner) {
            close(mHandle->fd);
            delete mHandle;
        }
    }

    if (mFd > 0) {
        close(mFd);
    }
}

int IonShadow::fd()
{
    return mFd;
}

struct Memory* IonShadow::handle()
{
    return mHandle;
}

//--------------------------------------------------
IonManager::IonManager()
{
    mIonFd = ion_open();
    if (mIonFd <= 0) {
        ALOGE("%s ion open failed", __func__);
    }
}

IonManager::~IonManager()
{
    if (mIonFd > 0) {
        close(mIonFd);
    }
}

int IonManager::allocMemory(MemoryDesc& desc, Memory** out)
{
    if (mIonFd <= 0 || out == NULL) {
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
    int sharedFd;
    ion_user_handle_t ion_hnd = -1;
    Memory* memory = NULL;

    desc.mSize = (desc.mSize + PAGE_SIZE) & (~(PAGE_SIZE - 1));
    int err = ion_alloc(mIonFd, desc.mSize, 8, 1, 0, &ion_hnd);
    if (err) {
        ALOGE("ion_alloc failed");
        return err;
    }

    err = ion_share(mIonFd, ion_hnd, &sharedFd);
    if (err) {
        ALOGE("ion_share failed");
        ion_free(mIonFd, ion_hnd);
        return err;
    }

    memory = new Memory(&desc, sharedFd);
    getPhys(memory);
    MemoryShadow* shadow = new IonShadow(sharedFd, memory, true);
    memory->shadow = (intptr_t)shadow;
    *out = memory;
    ion_free(mIonFd, ion_hnd);
    close(sharedFd);

    return 0;
}

int IonManager::getPhys(Memory* memory)
{
    if (mIonFd <= 0 || memory == NULL || memory->fd < 0) {
        ALOGE("%s invalid parameters", __func__);
        return -EINVAL;
    }

    int phyAddr = 0;
    ion_user_handle_t ion_hnd = -1;
    int err = ion_import(mIonFd, memory->fd, &ion_hnd);
    if (err) {
        ALOGE("ion_import failed");
        return -EINVAL;
    }

    phyAddr = ion_phys(mIonFd, ion_hnd);
    if (phyAddr == 0) {
        ALOGE("ion_phys failed");
        ion_free(mIonFd, ion_hnd);
        return -EINVAL;
    }

    memory->phys = phyAddr;
    ion_free(mIonFd, ion_hnd);
    return 0;
}

int IonManager::getVaddrs(Memory* memory)
{
    if (mIonFd <= 0 || memory == NULL || memory->fd < 0) {
        ALOGE("%s invalid parameters", __func__);
        return -EINVAL;
    }

    void* mappedAddress = mmap(0, memory->size,
        PROT_READ|PROT_WRITE, MAP_SHARED, memory->fd, 0);
    if (mappedAddress == MAP_FAILED) {
        ALOGE("Could not mmap %s", strerror(errno));
        return -EINVAL;
    }
    memory->base = (intptr_t)mappedAddress;

    return 0;
}

int IonManager::flushCache(Memory* memory)
{
    if (mIonFd <= 0 || memory == NULL || memory->fd < 0) {
        ALOGE("%s invalid parameters", __func__);
        return -EINVAL;
    }

    ion_sync_fd(mIonFd, memory->fd);

    return 0;
}

int IonManager::retainMemory(Memory* handle)
{
    if (handle == NULL || !handle->isValid()) {
        ALOGE("%s invalid handle", __func__);
        return -EINVAL;
    }

    MemoryShadow* shadow = (MemoryShadow*)(intptr_t)handle->shadow;
    if (handle->pid != getpid()) {
        shadow = new IonShadow(handle->fd, handle, false);
        handle->shadow = (intptr_t)shadow;
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

int IonManager::lock(Memory* handle, int /*usage*/,
        int /*l*/, int /*t*/, int /*w*/, int /*h*/, void** vaddr)
{
    int ret = 0;
    ret = verifyMemory(handle);
    if (ret != 0) {
        ALOGE("%s verify memory failed", __func__);
        return -EINVAL;
    }

    if (handle->base == 0) {
        getVaddrs(handle);
    }
    *vaddr = (void *)handle->base;

    return 0;
}

int IonManager::lockYCbCr(Memory* handle, int usage,
        int l, int t, int w, int h, android_ycbcr* ycbcr)
{
    int ret = 0;
    ret = verifyMemory(handle);
    if (ret != 0) {
        ALOGE("%s verify memory failed", __func__);
        return -EINVAL;
    }

    if (handle->base == 0) {
        getVaddrs(handle);
    }

    return MemoryManager::lockYCbCr(handle, usage, l, t, w, h ,ycbcr);
}

int IonManager::unlock(Memory* handle)
{
    int ret = 0;
    ret = verifyMemory(handle);
    if (ret != 0) {
        ALOGE("%s verify memory failed", __func__);
        return -EINVAL;
    }

    if (handle->flags & FLAGS_CPU) {
        flushCache(handle);
    }

    return 0;
}

}
