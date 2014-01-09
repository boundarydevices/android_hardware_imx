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
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cutils/log.h>
#include <cutils/atomic.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>
#include <ion/ion.h>

#include "gralloc_priv.h"

static int getIonFd(gralloc_module_t const *module)
{
    private_module_t* m = const_cast<private_module_t*>(
            reinterpret_cast<const private_module_t*>(module));
    if (m->ion_fd < 0)
        m->ion_fd = ion_open();
    return m->ion_fd;
}

int fsl_gralloc_map(gralloc_module_t const* module,
        buffer_handle_t handle,
        void** vaddr)
{
    private_handle_t* hnd = (private_handle_t*)handle;
    if (!(hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER)) {
        getIonFd(module);
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
    *vaddr = (void*)hnd->base;
    return 0;
}

int fsl_gralloc_unmap(gralloc_module_t const* module,
        buffer_handle_t handle)
{
    private_handle_t* hnd = (private_handle_t*)handle;
    if (!(hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER)) {
        getIonFd(module);
        void* base = (void*)hnd->base;
        size_t size = hnd->size;
        //ALOGD("unmapping from %p, size=%d", base, size);
        if (munmap(base, size) < 0) {
            ALOGE("Could not unmap %s", strerror(errno));
        }
    }
    hnd->base = 0;
    return 0;
}

int fsl_gralloc_register_buffer(gralloc_module_t const* module,
        buffer_handle_t handle)
{
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    private_handle_t* hnd = (private_handle_t*)handle;
    //ALOGD_IF(hnd->pid == getpid(),
    //        "Registering a buffer in the process that created it. "
    //        "This may cause memory ordering problems.");

    int ret = 0;
    if (hnd->pid != getpid()) {
        void *vaddr;
        ret = fsl_gralloc_map(module, handle, &vaddr);
    }
    if (ret == 0) {
        // increase the buffer counter in process.
        // ion_increfs(moudule->ion_fd, hnd->fd);
        // actually when call mmap, ion will increase ref count.
    }

    return 0;
}

int fsl_gralloc_unregister_buffer(gralloc_module_t const* module,
        buffer_handle_t handle)
{
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    private_handle_t* hnd = (private_handle_t*)handle;
    if (hnd->base != 0) {
        fsl_gralloc_unmap(module, handle);
    }

    // decrease the buffer counter in process.
    // ion_decrefs(moudule->ion_fd, hnd->fd);
    // when call munmap, ion will decrease ref count.

    return 0;
}

int fsl_gralloc_lock(gralloc_module_t const* module,
        buffer_handle_t handle, int usage,
        int l, int t, int w, int h,
        void** vaddr)
{
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    private_handle_t* hnd = (private_handle_t*)handle;
    *vaddr = (void*)hnd->base;

    //may call ion_lock to sync the buffer access across process.
    //ion_lock(moudule->ion_fd, hnd->fd);
    return 0;
}

int fsl_gralloc_unlock(gralloc_module_t const* module,
        buffer_handle_t handle)
{
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    //may call ion_lock to sync the buffer access across process.
    //ion_unlock(moudule->ion_fd, hnd->fd);
    return 0;
}

