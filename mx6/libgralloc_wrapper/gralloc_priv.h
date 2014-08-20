/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2009-2014 Freescale Semiconductor, Inc.
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

#ifndef GRALLOC_PRIV_H_
#define GRALLOC_PRIV_H_

#include <stdint.h>
#include <limits.h>
#include <sys/cdefs.h>
#include <hardware/gralloc.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>

#include <cutils/native_handle.h>
#include <linux/fb.h>

#define GRALLOC_HARDWARE_FB "fb"
#define GRALLOC_VIV_HARDWARE_MODULE_ID "gralloc_viv"
#define MAX_DISPLAY_DEVICE 4

#define  ALIGN_PIXEL_4(x)  ((x+ 3) & ~3)
#define  ALIGN_PIXEL_16(x)  ((x+ 15) & ~15)
#define  ALIGN_PIXEL_32(x)  ((x+ 31) & ~31)
#define  ALIGN_PIXEL_128(x)  ((x+ 127) & ~127)

/*****************************************************************************/
struct private_module_t;
struct private_handle_t;

class BufferManager;

struct gralloc_context_t {
    alloc_device_t  device;
    /* our private data here */
    BufferManager* module;
};

struct private_module_t {
    gralloc_module_t base;

    enum {
        // flag to indicate we'll post this buffer
        PRIV_USAGE_LOCKED_FOR_POST = 0x80000000
    };
};

/*****************************************************************************/

#ifdef __cplusplus
struct private_handle_t : public native_handle {
#else
struct private_handle_t {
    struct native_handle nativeHandle;
#endif

    enum {
        PRIV_FLAGS_FRAMEBUFFER =    0x00000001,
        PRIV_FLAGS_FRAMEBUFFER_X =  0x00000002,
        PRIV_FLAGS_FRAMEBUFFER_2X = 0x00000004,
        PRIV_FLAGS_USES_ION    =    0x00000008,
    };

    enum {
        LOCK_STATE_WRITE     =   1<<31,
        LOCK_STATE_MAPPED    =   1<<30,
        LOCK_STATE_READ_MASK =   0x3FFFFFFF
    };
/** do NOT change any element below **/
    int  fd;
    int  magic;
    int  flags;
    int  size;
    int  offset;
    int  base;
    int  phys;
    int  format;
    int  width;
    int  height;
    int  pid;

    int  usage;
    int  stride;
    int  reserved[4];

#ifdef __cplusplus
    static const int sNumInts = 16;
    static const int sNumFds = 1;
    static const int sMagic = 'pgpu';

    private_handle_t(int fd, int size, int flags) :
        fd(fd), magic(sMagic), flags(flags), size(size), offset(0),
        base(0),  phys(0),  format(0), width(0),
        height(0), pid(getpid())
    {
        version = sizeof(native_handle);
        numInts = sNumInts;
        numFds = sNumFds;
        //usage = 0;
        //stride = 0;
    }
    ~private_handle_t() {
        magic = 0;
    }

    static int validate(const native_handle* h) {
        const private_handle_t* hnd = (const private_handle_t*)h;
        if (!h || h->version != sizeof(native_handle) ||
                h->numInts != sNumInts || h->numFds != sNumFds ||
                hnd->magic != sMagic)
        {
            ALOGE("invalid gralloc handle (at %p)", h);
            return -EINVAL;
        }
        return 0;
    }

    static private_handle_t* dynamicCast(const native_handle* in) {
        if (validate(in) == 0) {
            return (private_handle_t*) in;
        }
        return NULL;
    }
#endif
};

#endif /* GRALLOC_PRIV_H_ */
