/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2013-2014 Freescale Semiconductor, Inc.
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

#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <cutils/ashmem.h>
#include <cutils/log.h>
#include <cutils/atomic.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>
#include <ion/ion.h>

#include "gralloc_priv.h"

extern int fsl_gralloc_unmap(gralloc_module_t const* module,
        buffer_handle_t handle);
extern int mapFrameBufferLocked(struct private_module_t* module);

static int fsl_gralloc_alloc_framebuffer_locked(alloc_device_t* dev,
        size_t size, int usage, buffer_handle_t* pHandle)
{
    private_module_t* m = reinterpret_cast<private_module_t*>(
            dev->common.module);

    // allocate the framebuffer
    if (m->framebuffer == NULL) {
        // initialize the framebuffer, the framebuffer is mapped once
        // and forever.
        int err = mapFrameBufferLocked(m);
        if (err < 0) {
            ALOGE("%s mapFrameBufferLocked failed", __FUNCTION__);
            return err;
        }
    }

    const uint32_t bufferMask = m->bufferMask;
    const uint32_t numBuffers = m->numBuffers;
    const size_t bufferSize = m->finfo.line_length * ALIGN_PIXEL_128(m->info.yres);
    if (numBuffers < 2) {
        ALOGE("%s framebuffer number less than 2", __FUNCTION__);
        return -ENOMEM;
    }

    if (bufferMask >= ((1LU<<numBuffers)-1)) {
        // We ran out of buffers.
        ALOGE("%s out of memory", __FUNCTION__);
        return -ENOMEM;
    }

    // create a "fake" handles for it
    intptr_t vaddr = intptr_t(m->framebuffer->base);
    private_handle_t* hnd = new private_handle_t(dup(m->framebuffer->fd), size,
            private_handle_t::PRIV_FLAGS_FRAMEBUFFER);

    // find a free slot
    for (uint32_t i=0 ; i<numBuffers ; i++) {
        if ((bufferMask & (1LU<<i)) == 0) {
            m->bufferMask |= (1LU<<i);
            break;
        }
        vaddr += bufferSize;
    }

    if (usage & GRALLOC_USAGE_HW_FBX) {
        hnd->flags |= private_handle_t::PRIV_FLAGS_FRAMEBUFFER_X;
    }

    hnd->base = vaddr;
    hnd->offset = vaddr - intptr_t(m->framebuffer->base);
    hnd->phys = intptr_t(m->framebuffer->phys) + hnd->offset;
    *pHandle = hnd;

    return 0;
}

static int fsl_gralloc_alloc_framebuffer(alloc_device_t* dev,
        size_t size, int usage, buffer_handle_t* pHandle)
{
    private_module_t* m = reinterpret_cast<private_module_t*>(
            dev->common.module);
    pthread_mutex_lock(&m->lock);
    int err = fsl_gralloc_alloc_framebuffer_locked(dev, size, usage, pHandle);
    pthread_mutex_unlock(&m->lock);
    return err;
}

static int fsl_gralloc_alloc_buffer(alloc_device_t* dev,
        size_t size, int usage, buffer_handle_t* pHandle)
{
    unsigned char *ptr = NULL;
    int sharedFd;
    int phyAddr;
    ion_user_handle_t ion_hnd = -1;
    size = (size + PAGE_SIZE) & (~(PAGE_SIZE - 1));

    private_module_t* m = reinterpret_cast<private_module_t*>(dev->common.module);
    int err = ion_alloc(m->ion_fd, size, 8, 1, 0, &ion_hnd);
    if (err) {
        ALOGE("ion_alloc failed");
        return err;
    }

    err = ion_map(m->ion_fd, ion_hnd, size, PROT_READ | PROT_WRITE,
                    MAP_SHARED, 0, &ptr, &sharedFd);
    if (err) {
        ALOGE("ion_alloc failed");
        return err;
    }

    phyAddr = ion_phys(m->ion_fd, ion_hnd);
    if (phyAddr == 0) {
        ALOGE("ion_phys failed");
        return -EINVAL;
    }

    private_handle_t* hnd = new private_handle_t(sharedFd, size,
                     private_handle_t::PRIV_FLAGS_USES_ION);
    hnd->base = (int)ptr;
    hnd->phys = phyAddr;
    *pHandle = hnd;
    ion_free(m->ion_fd, ion_hnd);

    return 0;
}

int fsl_gralloc_alloc(alloc_device_t* dev,
        int w, int h, int format, int usage,
        buffer_handle_t* pHandle, int* pStride)
{
    if (!pHandle || !pStride) {
        ALOGE("<%s,%d> invalide parameters", __FUNCTION__, __LINE__);
        return -EINVAL;
    }

    private_module_t* m = reinterpret_cast<private_module_t*>(dev->common.module);
    if (!m || m->ion_fd <= 0) {
        ALOGE("<%s,%d> m or m->gpu_device is NULL", __FUNCTION__, __LINE__);
        return -EINVAL;
    }

    int err = 0;
    size_t size, alignedw, alignedh, bpp = 0;
    switch (format) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
            bpp = 4;
        case HAL_PIXEL_FORMAT_RGB_888:
        case HAL_PIXEL_FORMAT_RGB_565:
            if (format == HAL_PIXEL_FORMAT_RGB_565) {
                bpp = 2;
            }
            else if (format == HAL_PIXEL_FORMAT_RGB_888) {
                bpp = 3;
            }

            alignedw = ALIGN_PIXEL_16(w);
            alignedh = ALIGN_PIXEL_16(h);
            size = alignedw * alignedh * bpp;
            break;

        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            alignedw = ALIGN_PIXEL_32(w);
            size = alignedw * h * 3 / 2;
            break;

        case HAL_PIXEL_FORMAT_YCbCr_420_P:
        case HAL_PIXEL_FORMAT_YV12: {
            alignedw = ALIGN_PIXEL_32(w);
            int c_stride = (alignedw/2+15)/16*16;
            size = alignedw * h + c_stride * h;
            } break;

        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            alignedw = ALIGN_PIXEL_32(w);
            size = alignedw * h * 2;
            break;

        default:
            ALOGE("%s unsupported format:0x%x", __FUNCTION__, format);
            return -EINVAL;
    }

    private_handle_t* hnd = NULL;
    if (usage & GRALLOC_USAGE_HW_FBX) {
        gralloc_context_t *ctx = (gralloc_context_t *)dev;
        if (ctx->ext_dev == NULL) {
            ALOGE("ctx->ext_dev == NULL");
            return -EINVAL;
        }

        err = fsl_gralloc_alloc_framebuffer(ctx->ext_dev, size, usage, (buffer_handle_t*)&hnd);
    }
    else if (usage & GRALLOC_USAGE_HW_FB) {
        err = fsl_gralloc_alloc_framebuffer(dev, size, usage, (buffer_handle_t*)&hnd);
    }
    else {
        err = fsl_gralloc_alloc_buffer(dev, size, usage, (buffer_handle_t*)&hnd);
    }

    hnd->width = w;
    hnd->height = h;
    hnd->format = format;
    //becaue private_handle_t doesn't contains stride.
    //hack it to set stride in flags high 16bit.
    hnd->flags |= (alignedw & 0xffff) << 16;
    hnd->pid = getpid();
    *pHandle = hnd;
    *pStride = alignedw;
    return err;
}

int fsl_gralloc_free(alloc_device_t* dev,
        buffer_handle_t handle)
{
    private_module_t* m = reinterpret_cast<private_module_t*>(dev->common.module);
    if (!m || m->ion_fd <= 0) {
        ALOGE("<%s,%d> m or m->gpu_device is NULL", __FUNCTION__, __LINE__);
        return -EINVAL;
    }

    if (private_handle_t::validate(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    private_handle_t const* hnd = reinterpret_cast<private_handle_t const*>(handle);
    if (hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER) {
        // free this buffer
        if (hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER_X) {
            gralloc_context_t *ctx = (gralloc_context_t *)dev;
            if (ctx->ext_dev == NULL) {
                ALOGE("ctx->ext_dev == NULL");
                return -EINVAL;
            }
            dev = ctx->ext_dev;
        }

        private_module_t* m = reinterpret_cast<private_module_t*>(
                dev->common.module);
        const size_t bufferSize = m->finfo.line_length * ALIGN_PIXEL_128(m->info.yres);
        int index = (hnd->base - m->framebuffer->base) / bufferSize;
        m->bufferMask &= ~(1<<index);
    }
    else {
        if (hnd->base) {
            fsl_gralloc_unmap((const gralloc_module_t*)m, const_cast<private_handle_t*>(hnd));
        }
    }

    close(hnd->fd);
    delete hnd;
    return 0;
}

