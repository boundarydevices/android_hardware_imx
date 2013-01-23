/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2010-2013 Freescale Semiconductor, Inc.
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

#include "gralloc_priv.h"
/*****************************************************************************/

int fb_device_open(const hw_module_t* module, const char* name,
        hw_device_t** device);

static int gralloc_device_open(const hw_module_t* module, const char* name,
        hw_device_t** device);

extern int gralloc_lock(gralloc_module_t const* module,
        buffer_handle_t handle, int usage,
        int l, int t, int w, int h,
        void** vaddr);

extern int gralloc_unlock(gralloc_module_t const* module, 
        buffer_handle_t handle);

extern int gralloc_register_buffer(gralloc_module_t const* module,
        buffer_handle_t handle);

extern int gralloc_unregister_buffer(gralloc_module_t const* module,
        buffer_handle_t handle);

/*****************************************************************************/

static struct hw_module_methods_t gralloc_module_methods = {
        open: gralloc_device_open
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
        registerBuffer: gralloc_register_buffer,
        unregisterBuffer: gralloc_unregister_buffer,
        lock: gralloc_lock,
        unlock: gralloc_unlock,
        perform: 0,
        reserved_proc: {0}
    },
    framebuffer: 0,
    numBuffers: 0,
    bufferMask: 0,
    lock: PTHREAD_MUTEX_INITIALIZER,
    currentBuffer: 0,
};

/*****************************************************************************/

static int gralloc_alloc(alloc_device_t* dev,
        int w, int h, int format, int usage,
        buffer_handle_t* pHandle, int* pStride)
{
    if (!pHandle || !pStride) {
        ALOGE("<%s,%d> invalide parameters", __FUNCTION__, __LINE__);
        return -EINVAL;
    }

    private_module_t* m = reinterpret_cast<private_module_t*>(dev->common.module);
    if (!m || !m->gpu_device) {
        ALOGE("<%s,%d> m or m->gpu_device is NULL", __FUNCTION__, __LINE__);
        return -EINVAL;
    }

    if (usage & GRALLOC_USAGE_HW_FBX) {
        gralloc_context_t *ctx = (gralloc_context_t *)dev;
        if (ctx->ext_dev == NULL) {
            ALOGE("ctx->ext_dev == NULL");
            return -EINVAL;
        }
        return m->gpu_device->alloc(ctx->ext_dev, w, h, format, usage, pHandle, pStride);
    }

    return m->gpu_device->alloc(dev, w, h, format, usage, pHandle, pStride);
}

static int gralloc_free(alloc_device_t* dev,
        buffer_handle_t handle)
{
    private_module_t* m = reinterpret_cast<private_module_t*>(dev->common.module);
    if (!m || !m->gpu_device) {
        ALOGE("<%s,%d> m or m->gpu_device is NULL", __FUNCTION__, __LINE__);
        return -EINVAL;
    }

    if (m->external_module != NULL && m->external_module->framebuffer != NULL) {
        private_handle_t* ext_fb = m->external_module->framebuffer;
        private_handle_t* priv_handle = (private_handle_t*)handle;
        if(priv_handle->base >= ext_fb->base && priv_handle->base < ext_fb->base + ext_fb->size) {
            gralloc_context_t *ctx = (gralloc_context_t *)dev;
            if (ctx->ext_dev == NULL) {
                ALOGW("ctx->ext_dev == NULL");
                return -EINVAL;
            }

            return m->gpu_device->free(ctx->ext_dev, handle);
        }
    }

    return m->gpu_device->free(dev, handle);
}

/*****************************************************************************/

static int gralloc_close(struct hw_device_t *dev)
{
    gralloc_context_t* ctx = reinterpret_cast<gralloc_context_t*>(dev);
    if (ctx) {
        /* TODO: keep a list of all buffer_handle_t created, and free them
         * all here.
         */
        private_module_t* m = reinterpret_cast<private_module_t*>(ctx->device.common.module);
        if(m && m->gpu_device){
           m->gpu_device->common.close((struct hw_device_t *)m->gpu_device);
           m->gpu_device = NULL;
        }

        free(ctx);
    }
    return 0;
}

int gralloc_device_open(const hw_module_t* module, const char* name,
        hw_device_t** device)
{
    int status = -EINVAL;
    hw_module_t *hw = const_cast<hw_module_t *>(module);

    private_module_t* m = reinterpret_cast<private_module_t*>(hw);
    if (!m->gpu_device) {
       hw_module_t const* gpu_module;;
       if (hw_get_module(GRALLOC_VIV_HARDWARE_MODULE_ID, &gpu_module) == 0) {
          status = gralloc_open(gpu_module, &m->gpu_device);
          if(status || !m->gpu_device){
             ALOGE("gralloc_device_open: gpu gralloc device open failed!");
          }
       }
    }

    if (!m->priv_dev) {
        gralloc_context_t *dev;
        dev = (gralloc_context_t*)malloc(sizeof(*dev));

        /* initialize our state here */
        memset(dev, 0, sizeof(*dev));

        /* initialize the procs */
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.version = 0;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = gralloc_close;

        dev->device.alloc   = gralloc_alloc;
        dev->device.free    = gralloc_free;

        gralloc_context_t* ext;
        ext = (gralloc_context_t*)malloc(sizeof(*ext));
        memset(ext, 0, sizeof(*ext));
        memcpy(ext, dev, sizeof(*ext));
        dev->ext_dev = (alloc_device_t*)ext;

        m->priv_dev = (alloc_device_t*)dev;
    }

    if (!strcmp(name, GRALLOC_HARDWARE_GPU0)) {
        *device = &m->priv_dev->common;
        status = 0;
    }
    else {
        status = fb_device_open(module, name, device);
    }

    return status;
}
