/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2013-2015 Freescale Semiconductor, Inc.
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

#include <graphics_ext.h>
#include <MemoryManager.h>

using namespace fsl;

#define  ALIGN_PIXEL_4(x)  ((x+ 3) & ~3)
#define  ALIGN_PIXEL_32(x)  ((x+ 31) & ~31)
extern int fb_device_open(const hw_module_t* module, const char* name,
        hw_device_t** device);

struct gralloc_context_t {
    alloc_device_t  device;
};

struct private_module_t {
    gralloc_module_t base;
};

int convertAndroidFormat(int format)
{
    int fslFormat = 0;
    switch (format) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
            fslFormat = FORMAT_RGBA8888;
            break;
        case HAL_PIXEL_FORMAT_RGBX_8888:
            fslFormat = FORMAT_RGBX8888;
            break;
        case HAL_PIXEL_FORMAT_BGRA_8888:
            fslFormat = FORMAT_BGRA8888;
            break;
        case HAL_PIXEL_FORMAT_RGB_888:
            fslFormat = FORMAT_RGB888;
            break;
        case HAL_PIXEL_FORMAT_RGB_565:
            fslFormat = FORMAT_RGB565;
            break;
        case HAL_PIXEL_FORMAT_YV12:
            fslFormat = FORMAT_YV12;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
            fslFormat = FORMAT_NV16;
            break;
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
            fslFormat = FORMAT_NV21;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            fslFormat = FORMAT_YUYV;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_P:
            fslFormat = FORMAT_I420;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            fslFormat = FORMAT_NV12;
            break;
        case HAL_PIXEL_FORMAT_BLOB:
            fslFormat = FORMAT_BLOB;
            break;
        default:
            ALOGE("%s invalid format:0x%x", __func__, format);
            break;
    }

    return fslFormat;
}

int gralloc_alloc(alloc_device_t* /*dev*/,
        int w, int h, int format, int usage,
        buffer_handle_t* handle, int* stride)
{
    if (!handle) {
        ALOGE("%s invalide parameters", __func__);
        return -EINVAL;
    }

    MemoryManager* pManager = MemoryManager::getInstance();
    if (pManager == NULL) {
	ALOGE("%s can't get memory manager", __func__);
        return -EINVAL;
    }

    if (format == HAL_PIXEL_FORMAT_YCbCr_420_888) {
        // now, take the flex format as NV12.
        // this format should only be known in framework.
        // private_handle in hal should not record this format.
        format = HAL_PIXEL_FORMAT_YCbCr_420_SP;
    }

    int flags = 0;
    if (usage & GRALLOC_USAGE_HW_FB) {
        usage &= ~GRALLOC_USAGE_HW_FB;
        flags |= FLAGS_FRAMEBUFFER;
        usage |= GRALLOC_USAGE_HW_COMPOSER
                | GRALLOC_USAGE_HW_2D
                | GRALLOC_USAGE_HW_RENDER;
    }

    if ((usage & GRALLOC_USAGE_SW_READ_MASK) != 0 ||
        (usage & GRALLOC_USAGE_SW_WRITE_MASK) != 0) {
        flags |= FLAGS_CPU;
    }

    Memory* memory = NULL;
    MemoryDesc desc;
    desc.mWidth = w;
    desc.mHeight = h;
    desc.mFormat = format;
    desc.mFslFormat = convertAndroidFormat(format);
    desc.mProduceUsage = usage;
    desc.mFlag = flags;

    if (desc.mFslFormat == FORMAT_BLOB) {
        // GPU can't recognize BLOB format, fake format to YUYV.
        // size = width * height * 2;
        // avoid height alignment issue.
        desc.mFormat = HAL_PIXEL_FORMAT_YCbCr_422_I;
        desc.mFslFormat = FORMAT_YUYV;
        desc.mWidth = desc.mWidth / 32;
        desc.mHeight = desc.mHeight * 16;
    }

    desc.checkFormat();

    int ret = pManager->allocMemory(desc, &memory);
    if (ret != 0) {
        ALOGE("%s alloc memory failed", __func__);
        return -EINVAL;
    }

    *handle = memory;
    if (stride != NULL) {
        *stride = memory->stride;
    }

    return 0;
}

int gralloc_free(alloc_device_t* /*dev*/, buffer_handle_t handle)
{
    MemoryManager* pManager = MemoryManager::getInstance();
    if (pManager == NULL) {
	ALOGE("%s can't get memory manager", __func__);
        return -EINVAL;
    }

    return pManager->releaseMemory((Memory*)handle);
}

int gralloc_register_buffer(gralloc_module_t const* /*module*/,
                                buffer_handle_t handle)
{
    MemoryManager* pManager = MemoryManager::getInstance();
    if (pManager == NULL) {
	ALOGE("%s can't get memory manager", __func__);
        return -EINVAL;
    }

    return pManager->retainMemory((Memory*)handle);
}

int gralloc_unregister_buffer(gralloc_module_t const* /*module*/,
        buffer_handle_t handle)
{
    MemoryManager* pManager = MemoryManager::getInstance();
    if (pManager == NULL) {
	ALOGE("%s can't get memory manager", __func__);
        return -EINVAL;
    }

    return pManager->releaseMemory((Memory*)handle);
}

int gralloc_lock(gralloc_module_t const* /*module*/,
        buffer_handle_t handle, int usage,
        int l, int t, int w, int h,
        void** vaddr)
{
    MemoryManager* pManager = MemoryManager::getInstance();
    if (pManager == NULL) {
	ALOGE("%s can't get memory manager", __func__);
        return -EINVAL;
    }

    return pManager->lock((Memory*)handle, usage, l, t, w, h, vaddr);
}

int gralloc_lockYCbCr(gralloc_module_t const* /*module*/,
        buffer_handle_t handle, int usage,
        int l, int t, int w, int h,
        android_ycbcr* ycbcr)
{
    MemoryManager* pManager = MemoryManager::getInstance();
    if (pManager == NULL) {
	ALOGE("%s can't get memory manager", __func__);
        return -EINVAL;
    }

    return pManager->lockYCbCr((Memory*)handle, usage, l, t, w, h, ycbcr);
}

int gralloc_unlock(gralloc_module_t const* /*module*/,
        buffer_handle_t handle)
{
    MemoryManager* pManager = MemoryManager::getInstance();
    if (pManager == NULL) {
	ALOGE("%s can't get memory manager", __func__);
        return -EINVAL;
    }

    return pManager->unlock((Memory*)handle);
}

/*****************************************************************************/
int gralloc_device_close(struct hw_device_t *dev)
{
    gralloc_context_t* ctx = reinterpret_cast<gralloc_context_t*>(dev);
    if (ctx) {
        free(ctx);
    }

    return 0;
}

int gralloc_device_open(const hw_module_t* module, const char* name,
        hw_device_t** device)
{
    int status = -EINVAL;
    hw_module_t *hw = const_cast<hw_module_t *>(module);
    if (hw == NULL) {
        ALOGE("%s invalid module", __func__);
        return status;
    }

    if (!strcmp(name, GRALLOC_HARDWARE_GPU0)) {
        gralloc_context_t *dev;
        dev = (gralloc_context_t*)malloc(sizeof(*dev));

        /* initialize our state here */
        memset(dev, 0, sizeof(*dev));

        /* initialize the procs */
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.version = 0;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = gralloc_device_close;

        dev->device.alloc   = gralloc_alloc;
        dev->device.free    = gralloc_free;

        *device = &dev->device.common;
        status = 0;
    }
    else {
        status = fb_device_open(module, name, device);
    }

    return status;
}

/*****************************************************************************/
static struct hw_module_methods_t gralloc_module_methods = {
        .open = gralloc_device_open
};

struct private_module_t HAL_MODULE_INFO_SYM = {
    .base = {
        .common = {
            .tag = HARDWARE_MODULE_TAG,
            .version_major = 1,
            .version_minor = 0,
            .id = GRALLOC_HARDWARE_MODULE_ID,
            .name = "Graphics Memory Allocator Module",
            .author = "Freescale Semiconductor, Inc.",
            .methods = &gralloc_module_methods,
            .dso = NULL,
            .reserved = {0}
        },
        .registerBuffer = gralloc_register_buffer,
        .unregisterBuffer = gralloc_unregister_buffer,
        .lock = gralloc_lock,
        .unlock = gralloc_unlock,
        .perform = 0,
        .lock_ycbcr = gralloc_lockYCbCr,
        .lockAsync = 0,
        .unlockAsync = 0,
        .lockAsync_ycbcr = 0,
        .reserved_proc = {0}
    },
};

