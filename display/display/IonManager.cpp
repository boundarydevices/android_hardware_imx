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

#include <string.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <cutils/log.h>
#include <ion/ion.h>
#include <linux/mxc_ion.h>
#include <linux/dma-buf.h>
#include <ion_4.12.h>
#ifdef CFG_SECURE_DATA_PATH
#include <linux/secure_ion.h>
#endif
#include <ion_ext.h>
#include "IonManager.h"

#define ION_DECODED_BUFFER_VPU_ALIGN 8
#define ION_HEAP_MASK ((1 << ION_CMA_HEAP_ID) | (1 << ION_CARVEOUT_HEAP_ID))

namespace fsl {

inline size_t roundUpToPageSize(size_t x) {
    return (x + (PAGE_SIZE-1)) & ~(PAGE_SIZE-1);
}

IonManager::IonManager()
    : mIonFd(-1), mHeapIds(0)
{
    mIonFd = ion_open();
    if (mIonFd <= 0) {
        ALOGE("%s ion open failed", __func__);
        return;
    }

    int heapCnt = 0;
    int ret = ion_query_heap_cnt(mIonFd, &heapCnt);
    if (ret != 0 || heapCnt == 0) {
        ALOGE("can't query heap count");
        return;
    }

    struct ion_heap_data ihd[heapCnt];
    memset(&ihd, 0, sizeof(ihd));
    ret = ion_query_get_heaps(mIonFd, heapCnt, &ihd);
    if (ret != 0) {
        ALOGE("can't get ion heaps");
        return;
    }

    for (int i=0; i<heapCnt; i++) {
        if (ihd[i].type == ION_HEAP_TYPE_DMA ||
             ihd[i].type == ION_HEAP_TYPE_CARVEOUT) {
            mHeapIds |=  1 << ihd[i].heap_id;
        }
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
    if (mIonFd <= 0 || out == NULL || mHeapIds == 0) {
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
    int err;
    Memory* memory = NULL;

    desc.mSize = (desc.mSize + PAGE_SIZE) & (~(PAGE_SIZE - 1));

#ifdef CFG_SECURE_DATA_PATH
    if (desc.mFlag & FLAGS_SECURE)
    {
        err = ion_alloc_fd(mIonFd,
            desc.mSize,
            ION_DECODED_BUFFER_VPU_ALIGN,
            DWL_ION_DECODED_BUFFER_DCSS_HEAP,
            0,
            &sharedFd);
    }
    else
#endif
    {
        err = ion_alloc_fd(mIonFd,
            desc.mSize,
            ION_DECODED_BUFFER_VPU_ALIGN,
            mHeapIds,
            0,
            &sharedFd);
    }

    if (err) {
        ALOGE("ion_alloc failed");
        return err;
    }

    memory = new Memory(&desc, sharedFd, -1);
    getPhys(memory);

    *out = memory;
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

    if (ion_is_legacy(mIonFd)) {
        phyAddr = ion_phys(mIonFd, memory->size, memory->fd);
    }
    else {
        struct dma_buf_phys dma_phys;
        ioctl(memory->fd, DMA_BUF_IOCTL_PHYS, &dma_phys);
        phyAddr = dma_phys.phys;
    }
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

    if (ion_is_legacy(mIonFd)) {
        ion_sync_fd(mIonFd, memory->fd);
    }
    else {
        struct dma_buf_sync dma_sync;
        dma_sync.flags = DMA_BUF_SYNC_RW | DMA_BUF_SYNC_END;
        ioctl(memory->fd, DMA_BUF_IOCTL_SYNC, &dma_sync);
    }

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
