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

#include <gralloc_priv.h>
#include <BufferManager.h>

/*****************************************************************************/
int BufferManager::gralloc_alloc(alloc_device_t* dev,
        int w, int h, int format, int usage,
        buffer_handle_t* pHandle, int* pStride)
{
    if (!pHandle || !pStride || dev == NULL) {
        ALOGE("<%s,%d> invalide parameters", __FUNCTION__, __LINE__);
        return -EINVAL;
    }

    gralloc_context_t* ctx = reinterpret_cast<gralloc_context_t*>(dev);
    BufferManager* m = ctx->module;
    if (m == NULL) {
        ALOGE("%s cat't get buffer manager", __FUNCTION__);
        return -EINVAL;
    }

    return m->alloc(w, h, format, usage, pHandle, pStride);
}

int BufferManager::gralloc_free(alloc_device_t* dev,
        buffer_handle_t handle)
{
    if (dev == NULL) {
        ALOGE("<%s,%d> invalide parameters", __FUNCTION__, __LINE__);
        return -EINVAL;
    }

    gralloc_context_t* ctx = reinterpret_cast<gralloc_context_t*>(dev);
    BufferManager* m = ctx->module;
    if (m == NULL) {
        ALOGE("%s cat't get buffer manager", __FUNCTION__);
        return -EINVAL;
    }

    if (m->validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    return m->free(handle);
}

int BufferManager::gralloc_register_buffer(gralloc_module_t const* module,
                                buffer_handle_t handle)
{
    if (module == NULL) {
        ALOGE("<%s,%d> invalide parameters", __FUNCTION__, __LINE__);
        return -EINVAL;
    }

    BufferManager* m = BufferManager::getInstance();
    if (m == NULL) {
        ALOGE("%s cat't get buffer manager", __FUNCTION__);
        return -EINVAL;
    }

    if (m->validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    return m->registerBuffer(handle);
}

int BufferManager::gralloc_unregister_buffer(gralloc_module_t const* module,
        buffer_handle_t handle)
{
    if (module == NULL) {
        ALOGE("<%s,%d> invalide parameters", __FUNCTION__, __LINE__);
        return -EINVAL;
    }

    BufferManager* m = BufferManager::getInstance();
    if (m == NULL) {
        ALOGE("%s cat't get buffer manager", __FUNCTION__);
        return -EINVAL;
    }

    if (m->validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    return m->unregisterBuffer(handle);
}

int BufferManager::gralloc_lock(gralloc_module_t const* module,
        buffer_handle_t handle, int usage,
        int l, int t, int w, int h,
        void** vaddr)
{
    if (module == NULL) {
        ALOGE("<%s,%d> invalide parameters", __FUNCTION__, __LINE__);
        return -EINVAL;
    }

    BufferManager* m = BufferManager::getInstance();
    if (m == NULL) {
        ALOGE("%s cat't get buffer manager", __FUNCTION__);
        return -EINVAL;
    }

    if (m->validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    return m->lock(handle, usage, l, t, w, h, vaddr);
}

int BufferManager::gralloc_unlock(gralloc_module_t const* module,
        buffer_handle_t handle)
{
    if (module == NULL) {
        ALOGE("<%s,%d> invalide parameters", __FUNCTION__, __LINE__);
        return -EINVAL;
    }

    BufferManager* m = BufferManager::getInstance();
    if (m == NULL) {
        ALOGE("%s cat't get buffer manager", __FUNCTION__);
        return -EINVAL;
    }

    if (m->validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    return m->unlock(handle);
}

/*****************************************************************************/
int BufferManager::gralloc_device_close(struct hw_device_t *dev)
{
    gralloc_context_t* ctx = reinterpret_cast<gralloc_context_t*>(dev);
    if (ctx) {
        /* TODO: keep a list of all buffer_handle_t created, and free them
         * all here.
         */
        ::free(ctx);
    }
    return 0;
}

int BufferManager::gralloc_device_open(const hw_module_t* module, const char* name,
        hw_device_t** device)
{
    int status = -EINVAL;
    hw_module_t *hw = const_cast<hw_module_t *>(module);
    if (hw == NULL) {
        ALOGE("%s invalid module", __FUNCTION__);
        return status;
    }

    BufferManager* m = BufferManager::getInstance();
    if (m == NULL) {
        ALOGE("%s invalid module.", __FUNCTION__);
        return -EINVAL;
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
        dev->module = m;

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
        open: BufferManager::gralloc_device_open
};

struct private_module_t HAL_MODULE_INFO_SYM = {
    base: {
        common: {
            tag: HARDWARE_MODULE_TAG,
            version_major: 1,
            version_minor: 0,
            id: GRALLOC_HARDWARE_MODULE_ID,
            name: "Graphics Memory Allocator Module",
            author: "The Android Open Source Project",
            methods: &gralloc_module_methods,
            dso: NULL,
            reserved: {0}
        },
        registerBuffer: BufferManager::gralloc_register_buffer,
        unregisterBuffer: BufferManager::gralloc_unregister_buffer,
        lock: BufferManager::gralloc_lock,
        unlock: BufferManager::gralloc_unlock,
        perform: 0,
        lock_ycbcr: 0,
        lockAsync: 0,
        unlockAsync: 0,
        lockAsync_ycbcr: 0,
        reserved_proc: {0}
    },
};

