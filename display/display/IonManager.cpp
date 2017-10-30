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

#include <inttypes.h>
#include <sys/mman.h>
#include <cutils/log.h>
#include <ion/ion.h>
#include <linux/mxc_ion.h>
#include <ion_ext.h>
#include "IonManager.h"

namespace fsl {

inline size_t roundUpToPageSize(size_t x) {
    return (x + (PAGE_SIZE-1)) & ~(PAGE_SIZE-1);
}

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

    uint64_t phyAddr = 0;

    phyAddr = ion_phys(mIonFd, memory->size, memory->fd);
    if (phyAddr == 0) {
        ALOGE("ion_phys failed");
        return -EINVAL;
    }

    memory->phys = phyAddr;
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
    memory->base = (uintptr_t)mappedAddress;

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
        int /*l*/, int /*t*/, int /*w*/, int /*h*/, android_ycbcr* ycbcr)
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
