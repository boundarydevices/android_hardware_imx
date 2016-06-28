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


#include <BufferManager.h>
#include <ion/ion.h>

using namespace android;

inline size_t roundUpToPageSize(size_t x) {
    return (x + (PAGE_SIZE-1)) & ~(PAGE_SIZE-1);
}

CPUBufferManager::CPUBufferManager()
{
    mIonFd = ion_open();
    if (mIonFd <= 0) {
        ALOGE("%s ion open failed", __FUNCTION__);
    }
}

CPUBufferManager::~CPUBufferManager()
{
    if (mIonFd > 0) {
        close(mIonFd);
    }
}

private_handle_t* CPUBufferManager::createPrivateHandle(int fd,
                             int size, int flags)
{
    private_handle_t* hnd = new private_handle_t(dup(fd), size, flags);
    return hnd;
}

void CPUBufferManager::destroyPrivateHandle(private_handle_t* handle)
{
    if (validateHandle(handle) < 0) {
        return;
    }

    close(handle->fd);
    delete handle;
}

int CPUBufferManager::validateHandle(buffer_handle_t handle)
{
    return private_handle_t::validate(handle);
}

int CPUBufferManager::allocBuffer(int w, int h, int format, int usage,
                                  int alignW, int /*alignH*/, size_t size,
                                  buffer_handle_t* handle, int* stride)
{
    if (!handle || !stride) {
        ALOGE("<%s,%d> invalide parameters", __FUNCTION__, __LINE__);
        return -EINVAL;
    }

    private_handle_t* hnd = NULL;
    int err = 0;
    if (usage & GRALLOC_USAGE_HW_FB) {
        err = allocFramebuffer(size, usage,
                              (buffer_handle_t*)&hnd);
    }
    else if (usage & GRALLOC_USAGE_FORCE_CONTIGUOUS) {
        err = allocBufferByIon(size, usage,
                          (buffer_handle_t*)&hnd);
    }
    else {
        err = allocBuffer(size, usage,
                          (buffer_handle_t*)&hnd);
    }

    if (err != 0) {
        ALOGE("%s alloc failed", __FUNCTION__);
        return err;
    }

    hnd->width = w;
    hnd->height = h;
    hnd->format = format;
    hnd->usage = usage;
    hnd->stride = alignW;
    //becaue private_handle_t doesn't contains stride.
    //hack it to set stride in flags high 16bit.
    hnd->pid = getpid();
    *handle = hnd;
    *stride = alignW;

    return err;
}

int CPUBufferManager::freeBuffer(buffer_handle_t handle)
{
    if (validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    unmapBuffer(handle);
    private_handle_t* hnd = (private_handle_t*)handle;
    destroyPrivateHandle(hnd);
    return 0;
}

int CPUBufferManager::allocBuffer(size_t size, int /*usage*/,
                 buffer_handle_t* pHandle)
{
    int err = 0;
    int fd = -1;

    size = roundUpToPageSize(size);

    fd = ashmem_create_region("gralloc-buffer", size);
    if (fd < 0) {
        ALOGE("couldn't create ashmem (%s)", strerror(-errno));
        err = -errno;
    }

    if (err == 0) {
        private_handle_t* hnd = new private_handle_t(fd, size, 0);
        err = mapBuffer(hnd);
        if (err == 0) {
            *pHandle = hnd;
        }
    }

    ALOGE_IF(err, "gralloc failed err=%s", strerror(-err));

    return err;
}

int CPUBufferManager::allocBufferByIon(size_t size, int /*usage*/,
                 buffer_handle_t* pHandle)
{
    if (mIonFd <= 0) {
        ALOGE("ion fd is invalid");
        return -EINVAL;
    }

    unsigned char *ptr = NULL;
    int sharedFd;
    int phyAddr;
    ion_user_handle_t ion_hnd = -1;
    size = (size + PAGE_SIZE) & (~(PAGE_SIZE - 1));

    int err = ion_alloc(mIonFd, size, 8, 1, 0, &ion_hnd);
    if (err) {
        ALOGE("ion_alloc failed");
        return err;
    }

    err = ion_map(mIonFd, ion_hnd, size, PROT_READ | PROT_WRITE,
                    MAP_SHARED, 0, &ptr, &sharedFd);
    if (err) {
        ALOGE("ion_map failed");
        ion_free(mIonFd, ion_hnd);
        if (ptr != MAP_FAILED) {
            munmap(ptr, size);
        }
        if (sharedFd > 0) {
            close(sharedFd);
        }
        return err;
    }

    phyAddr = ion_phys(mIonFd, ion_hnd);
    if (phyAddr == 0) {
        ALOGE("ion_phys failed");
        ion_free(mIonFd, ion_hnd);
        if (ptr != MAP_FAILED) {
            munmap(ptr, size);
        }
        close(sharedFd);
        return -EINVAL;
    }

    private_handle_t* hnd = new private_handle_t(sharedFd, size,
                     private_handle_t::PRIV_FLAGS_USES_ION);
    hnd->base = (uintptr_t)ptr;
    hnd->phys = phyAddr;
    *pHandle = hnd;
    ion_free(mIonFd, ion_hnd);

    return 0;
}

int CPUBufferManager::unmapBuffer(buffer_handle_t handle)
{
    if (validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    private_handle_t* hnd = (private_handle_t*)handle;
    void* base = (void*)hnd->base;
    size_t size = hnd->size;
    //ALOGD("unmapping from %p, size=%d", base, size);
    if (munmap(base, size) < 0) {
        ALOGE("Could not unmap %s", strerror(errno));
    }
    hnd->base = 0;
    return 0;
}

int CPUBufferManager::mapBuffer(buffer_handle_t handle)
{
    if (validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    private_handle_t* hnd = (private_handle_t*)handle;
    if (!(hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER)) {
        size_t size = hnd->size;
        void* mappedAddress = mmap(0, size,
                PROT_READ|PROT_WRITE, MAP_SHARED, hnd->fd, 0);
        if (mappedAddress == MAP_FAILED) {
            ALOGE("Could not mmap %s", strerror(errno));
            return -errno;
        }
        hnd->base = intptr_t(mappedAddress) + hnd->offset;
        //ALOGD("gralloc_map() succeeded fd=%d, off=%d, size=%d, vaddr=%p",
        //        hnd->fd, hnd->offset, hnd->size, mappedAddress);
    }
    return 0;
}

int CPUBufferManager::registerBuffer(buffer_handle_t handle)
{
    if (validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    private_handle_t* hnd = (private_handle_t*)handle;
    //ALOGD_IF(hnd->pid == getpid(),
    //        "Registering a buffer in the process that created it. "
    //        "This may cause me

    int ret = 0;
    if (hnd->pid != getpid()) {
        void *vaddr;
        ret = mapBuffer(handle);
        hnd->pid = getpid();
    }
    if (ret == 0) {
        // increase the buffer counter in process.
        // ion_increfs(moudule->ion_fd, hnd->fd);
        // actually when call mmap, ion will increase ref count.
    }

    return ret;
}

int CPUBufferManager::unregisterBuffer(buffer_handle_t handle)
{
    if (validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    private_handle_t* hnd = (private_handle_t*)handle;
    if (hnd->base != 0) {
        unmapBuffer(handle);
    }

    // decrease the buffer counter in process.
    // ion_decrefs(moudule->ion_fd, hnd->fd);
    // when call munmap, ion will decrease ref count.

    return 0;
}

int CPUBufferManager::lock(buffer_handle_t handle, int /*usage*/,
            int /*l*/, int /*t*/, int /*w*/, int /*h*/,
            void** vaddr)
{
    if (validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    private_handle_t* hnd = (private_handle_t*)handle;
    *vaddr = (void*)hnd->base;

    //may call ion_lock to sync the buffer access across process.
    //ion_lock(moudule->ion_fd, hnd->fd);
    return 0;
}


int CPUBufferManager::lockYCbCr(buffer_handle_t handle, int /*usage*/,
            int /*l*/, int /*t*/, int /*w*/, int /*h*/,
        android_ycbcr* ycbcr)
{
    if (ycbcr == NULL) {
        return 0;
    }

    if (validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    private_handle_t* hnd = (private_handle_t*)handle;

    switch (hnd->format) {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            ycbcr->ystride = hnd->stride;
            ycbcr->cstride = ycbcr->ystride;
            ycbcr->y = (void*)hnd->base;
            ycbcr->cb = (void*)(hnd->base + hnd->stride*hnd->height);
            ycbcr->cr = (void*)((uintptr_t)ycbcr->cb + 1);
            ycbcr->chroma_step = 2;
            break;

        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
            ycbcr->ystride = hnd->stride;
            ycbcr->cstride = ycbcr->ystride;
            ycbcr->y = (void*)hnd->base;
            ycbcr->cr = (void*)(hnd->base + hnd->stride*hnd->height);
            ycbcr->cb = (void*)((uintptr_t)ycbcr->cr + 1);
            ycbcr->chroma_step = 2;
            break;

        case HAL_PIXEL_FORMAT_YCbCr_420_P:
            ycbcr->ystride = hnd->stride;
            ycbcr->cstride = ycbcr->ystride / 2;
            ycbcr->y = (void*)hnd->base;
            ycbcr->cb = (void*)(hnd->base + hnd->stride*hnd->height);
            ycbcr->cr = (void*)((uintptr_t)ycbcr->cb + ycbcr->cstride*hnd->height/2);
            ycbcr->chroma_step = 1;
            break;

        case HAL_PIXEL_FORMAT_YV12:
            ycbcr->ystride = hnd->stride;
            ycbcr->cstride = ycbcr->ystride / 2;
            ycbcr->y = (void*)hnd->base;
            ycbcr->cr = (void*)(hnd->base + hnd->stride*hnd->height);
            ycbcr->cb = (void*)((uintptr_t)ycbcr->cr + ycbcr->cstride*hnd->height/2);
            ycbcr->chroma_step = 1;
            break;

        default:
            ALOGE("%s not support format:0x%x", __func__, hnd->format);
            return -EINVAL;
    }

    return 0;
}

int CPUBufferManager::unlock(buffer_handle_t handle)
{
    if (validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    private_handle_t* hnd = (private_handle_t*)handle;

    //may call ion_lock to sync the buffer access across process.
    //ion_unlock(moudule->ion_fd, hnd->fd);
    return 0;
}

