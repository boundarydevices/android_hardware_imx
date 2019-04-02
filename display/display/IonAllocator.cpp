/*
 * Copyright 2018 NXP.
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
#include <ion_4.12.h>
#include <linux/dma-buf.h>
#ifdef CFG_SECURE_DATA_PATH
#include <linux/secure_ion.h>
#endif
#include <ion_ext.h>
#include "IonAllocator.h"

namespace fsl {

IonAllocator* IonAllocator::sInstance(0);
Mutex IonAllocator::sLock(Mutex::PRIVATE);

IonAllocator* IonAllocator::getInstance()
{
    Mutex::Autolock _l(sLock);
    if (sInstance != NULL) {
        return sInstance;
    }

    sInstance = new IonAllocator();

    return sInstance;
}

IonAllocator::IonAllocator()
    : mIonFd(-1), mCCHeapIds(0),
      mCNHeapIds(0), mNCHeapIds(0),
      mSeHeapIds(0)
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

    // add heap ids from heap type.
    for (int i=0; i<heapCnt; i++) {
        if (ihd[i].type == ION_HEAP_TYPE_DMA ||
             ihd[i].type == ION_HEAP_TYPE_CARVEOUT) {
            mCNHeapIds |=  1 << ihd[i].heap_id;
            continue;
        }
        if (ihd[i].type == ION_HEAP_TYPE_SYSTEM) {
            mNCHeapIds |= 1 << ihd[i].heap_id;
            continue;
        }
        if (ihd[i].type == ION_HEAP_TYPE_SYSTEM_CONTIG) {
            mCCHeapIds |= 1 << ihd[i].heap_id;
        }
    }

#ifdef CFG_SECURE_DATA_PATH
    for (int i=0; i<heapCnt; i++) {
        if ((ihd[i].type == ION_HEAP_TYPE_UNMAPPED) &&
            (!memcmp(ihd[i].name,
                DWL_ION_DECODED_BUFFER_DISPLAY_HEAP_NAME,
                sizeof(DWL_ION_DECODED_BUFFER_DISPLAY_HEAP_NAME)))) {
            mSeHeapIds |=  1 << ihd[i].heap_id;
        }
    }
#endif
}

IonAllocator::~IonAllocator()
{
    if (mIonFd > 0) {
        close(mIonFd);
    }
}

int IonAllocator::allocMemory(int size, int align, int flags)
{
    int ret = 0;
    int fd = -1;
    int heapIds = 0;
    int ion_flags = 0;

    // contiguous memory includes cacheable/non-cacheable.
    if (flags & MFLAGS_CONTIGUOUS) {
        heapIds = mCCHeapIds | mCNHeapIds;
        if (flags & MFLAGS_CACHEABLE)
            ion_flags = ION_FLAG_CACHED;
    }
    else if (flags & MFLAGS_SECURE) {
        heapIds = mSeHeapIds;
    }
    // cacheable memory includes contiguous/non-contiguous.
    else if (flags & MFLAGS_CACHEABLE) {
        heapIds = mCCHeapIds | mNCHeapIds;
        ion_flags = ION_FLAG_CACHED;
    }
    else {
        ALOGE("%s invalid flags:0x%x", __func__, flags);
        return fd;
    }

    if ((mIonFd <= 0) || (heapIds == 0)) {
        ALOGE("%s invalid parameters", __func__);
        return fd;
    }

    // VPU decoder needs 32k physical address alignment.
    // But align parameter can't take effect to ensure alignment.
    // And ION driver also can't ensure physical address alignment.
    size = (size + (PAGE_SIZE << 3)) & (~((PAGE_SIZE << 3) - 1));
    ret = ion_alloc_fd(mIonFd, size, align, heapIds, ion_flags, &fd);
    if (ret != 0) {
        ALOGE("ion_alloc failed 0x%08X",ret);
    }

    return fd;
}

int IonAllocator::getPhys(int fd, int size, uint64_t& addr)
{
    uint64_t phyAddr = -1;

    if (mIonFd <= 0 || fd < 0) {
        ALOGE("%s invalid parameters", __func__);
        return -EINVAL;
    }

    if (ion_is_legacy(mIonFd)) {
        phyAddr = ion_phys(mIonFd, size, fd);
        if (phyAddr == 0) {
            ALOGE("%s ion_phys failed",__func__);
            return -EINVAL;
        }
    }
    else {
        struct dma_buf_phys dma_phys;
        if (ioctl(fd, DMA_BUF_IOCTL_PHYS, &dma_phys) < 0) {
            ALOGE("%s DMA_BUF_IOCTL_PHYS failed",__func__);
            return -EINVAL;
        }
        phyAddr = dma_phys.phys;
    }

    addr = phyAddr;
    return 0;
}

int IonAllocator::getVaddrs(int fd, int size, uint64_t& addr)
{
    if (mIonFd <= 0 || fd < 0) {
        ALOGE("%s invalid parameters", __func__);
        return -EINVAL;
    }

    void* vaddr = mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (vaddr == MAP_FAILED) {
        ALOGE("Could not mmap %s", strerror(errno));
        return -EINVAL;
    }

    addr = (uintptr_t)vaddr;
    return 0;
}

int IonAllocator::flushCache(int fd)
{
    if (mIonFd <= 0 || fd < 0) {
        ALOGE("%s invalid parameters", __func__);
        return -EINVAL;
    }

    if (ion_is_legacy(mIonFd)) {
        if (ion_sync_fd(mIonFd, fd) < 0) {
            ALOGE("%s ION_IOC_SYNC failed",__func__);
            return -EINVAL;
        }
    }
    else {
        struct dma_buf_sync dma_sync;
        dma_sync.flags = DMA_BUF_SYNC_RW | DMA_BUF_SYNC_END;
        if (ioctl(fd, DMA_BUF_IOCTL_SYNC, &dma_sync) < 0) {
            ALOGE("%s DMA_BUF_IOCTL_SYNC failed",__func__);
            return -EINVAL;
        }
    }

    return 0;
}

}
