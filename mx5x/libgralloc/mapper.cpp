/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
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


#include <errno.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ion/ion.h>

#include <cutils/log.h>
#include <cutils/atomic.h>
#include <hardware/hardware.h>
#include <hardware/gralloc.h>

#include "gralloc_priv.h"

static int gralloc_map(gralloc_module_t const* module,
        buffer_handle_t handle,
        void** vaddr)
{
    private_handle_t* hnd = (private_handle_t*)handle;
    if (!(hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER)) {
        int fd;
        size_t size = hnd->size;

        fd = ion_open();
        if(fd < 0) {
            ALOGE("gralloc_map() Cannot open ion err = %d", errno);
            return -errno;
        }
        void* mappedAddress = mmap(0, size,
                PROT_READ|PROT_WRITE, MAP_SHARED, hnd->fd, 0);
        if (mappedAddress == MAP_FAILED) {
            ALOGE("Could not mmap handle %p, fd=%d (%s)",
                    handle, hnd->fd, strerror(errno));
            hnd->base = 0;
            return -errno;
        }
        hnd->base = intptr_t(mappedAddress);
        memset(mappedAddress, 0, size);
        //ALOGD("gralloc_map() succeeded fd=%d, off=%d, size=%d, vaddr=%p", 
        //        hnd->fd, hnd->offset, hnd->size, mappedAddress);
        close(fd);
    }
    *vaddr = (void*)hnd->base;
    return 0;
}

static int gralloc_unmap(gralloc_module_t const* module,
        buffer_handle_t handle)
{
    private_handle_t* hnd = (private_handle_t*)handle;
    if (!(hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER)) {
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

static pthread_mutex_t sMapLock = PTHREAD_MUTEX_INITIALIZER; 

int gralloc_register_buffer(gralloc_module_t const* module,
        buffer_handle_t handle)
{
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    // In this implementation, we don't need to do anything here

    /* NOTE: we need to initialize the buffer as not mapped/not locked
     * because it shouldn't when this function is called the first time
     * in a new process. Ideally these flags shouldn't be part of the
     * handle, but instead maintained in the kernel or at least 
     * out-of-line
     */ 

    // if this handle was created in this process, then we keep it as is.
    private_handle_t* hnd = (private_handle_t*)handle;
    if (hnd->pid != getpid()) {
        hnd->base = 0;
        hnd->lockState  = 0;
        hnd->writeOwner = 0;
    }
    return 0;
}

int gralloc_unregister_buffer(gralloc_module_t const* module,
        buffer_handle_t handle)
{
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    /*
     * If the buffer has been mapped during a lock operation, it's time
     * to un-map it. It's an error to be here with a locked buffer.
     * NOTE: the framebuffer is handled differently and is never unmapped.
     */

    private_handle_t* hnd = (private_handle_t*)handle;
    
    ALOGE_IF(hnd->lockState & private_handle_t::LOCK_STATE_READ_MASK,
            "[unregister] handle %p still locked (state=%08x)",
            hnd, hnd->lockState);

    // never unmap buffers that were created in this process
    if (hnd->pid != getpid()) {
        if (hnd->lockState & private_handle_t::LOCK_STATE_MAPPED) {
            gralloc_unmap(module, handle);
        }
        hnd->base = 0;
        hnd->lockState  = 0;
        hnd->writeOwner = 0;
    }
    return 0;
}

int terminateBuffer(gralloc_module_t const* module,
        private_handle_t* hnd)
{
    /*
     * If the buffer has been mapped during a lock operation, it's time
     * to un-map it. It's an error to be here with a locked buffer.
     */

    ALOGE_IF(hnd->lockState & private_handle_t::LOCK_STATE_READ_MASK,
            "[terminate] handle %p still locked (state=%08x)",
            hnd, hnd->lockState);
    
    if (hnd->lockState & private_handle_t::LOCK_STATE_MAPPED) {
        // this buffer was mapped, unmap it now
        if ((hnd->flags & private_handle_t::PRIV_FLAGS_USES_ION) &&
            (hnd->pid == getpid())) {
            // ... unless it's a "master" ion buffer, that is a buffer
            // mapped in the process it's been allocated.
            // (see gralloc_alloc_buffer())
        } else {
            gralloc_unmap(module, hnd);
        }
    }

    return 0;
}

int gralloc_lock(gralloc_module_t const* module,
        buffer_handle_t handle, int usage,
        int l, int t, int w, int h,
        void** vaddr)
{
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    int err = 0;
    private_handle_t* hnd = (private_handle_t*)handle;
    int32_t current_value, new_value;
    int retry;

    do {
        current_value = hnd->lockState;
        new_value = current_value;

        if (current_value & private_handle_t::LOCK_STATE_WRITE) {
            // already locked for write 
            ALOGE("handle %p already locked for write", handle);
            return -EBUSY;
        } else if (current_value & private_handle_t::LOCK_STATE_READ_MASK) {
            // already locked for read
            if (usage & (GRALLOC_USAGE_SW_WRITE_MASK | GRALLOC_USAGE_HW_RENDER)) {
                ALOGE("handle %p already locked for read", handle);
                return -EBUSY;
            } else {
                // this is not an error
                //ALOGD("%p already locked for read... count = %d", 
                //        handle, (current_value & ~(1<<31)));
            }
        }

        // not currently locked
        if (usage & (GRALLOC_USAGE_SW_WRITE_MASK | GRALLOC_USAGE_HW_RENDER)) {
            // locking for write
            new_value |= private_handle_t::LOCK_STATE_WRITE;
        }
        new_value++;

        retry = android_atomic_cmpxchg(current_value, new_value, 
                (volatile int32_t*)&hnd->lockState);
    } while (retry);

    if (new_value & private_handle_t::LOCK_STATE_WRITE) {
        // locking for write, store the tid
        hnd->writeOwner = gettid();
    }

    if (usage & (GRALLOC_USAGE_SW_READ_MASK | GRALLOC_USAGE_SW_WRITE_MASK)) {
        if (!(current_value & private_handle_t::LOCK_STATE_MAPPED)) {
            // we need to map for real
            pthread_mutex_t* const lock = &sMapLock;
            pthread_mutex_lock(lock);
            if (!(hnd->lockState & private_handle_t::LOCK_STATE_MAPPED)) {
                err = gralloc_map(module, handle, vaddr);
                if (err == 0) {
                    android_atomic_or(private_handle_t::LOCK_STATE_MAPPED,
                            (volatile int32_t*)&(hnd->lockState));
                }
            }
            pthread_mutex_unlock(lock);
        }
        *vaddr = (void*)hnd->base;
    }

    return err;
}

int gralloc_unlock(gralloc_module_t const* module, 
        buffer_handle_t handle)
{
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    private_handle_t* hnd = (private_handle_t*)handle;
    int32_t current_value, new_value;

    do {
        current_value = hnd->lockState;
        new_value = current_value;

        if (current_value & private_handle_t::LOCK_STATE_WRITE) {
            // locked for write
            if (hnd->writeOwner == gettid()) {
                hnd->writeOwner = 0;
                new_value &= ~private_handle_t::LOCK_STATE_WRITE;
            }
        }

        if ((new_value & private_handle_t::LOCK_STATE_READ_MASK) == 0) {
            ALOGE("handle %p not locked", handle);
            return -EINVAL;
        }

        new_value--;

    } while (android_atomic_cmpxchg(current_value, new_value, 
            (volatile int32_t*)&hnd->lockState));

    return 0;
}

int gralloc_perform(struct gralloc_module_t const* module,
                    int operation, ... )
{
    return 0;
}

