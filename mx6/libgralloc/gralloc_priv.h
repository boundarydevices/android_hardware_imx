/*
 * Copyright (C) 2008 The Android Open Source Project
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

#define  ALIGN_PIXEL(x)  ((x+ 31) & ~31)
/** z430 core need 4k aligned memory, since xres has been 32 aligned, make yres
    to 128 aligned will meet this request for all pixel format (RGB565,RGB888,etc.) */
#define  ALIGN_PIXEL_128(x)  ((x+ 127) & ~127)
/*****************************************************************************/

struct private_module_t;
struct private_handle_t;

struct private_module_t {
    gralloc_module_t base;

    private_handle_t* framebuffer;
    uint32_t flags;
    uint32_t numBuffers;
    uint32_t bufferMask;
    pthread_mutex_t lock;
    buffer_handle_t currentBuffer;
    int pmem_master;
    void* pmem_master_base;
    unsigned long master_phys;

    struct fb_var_screeninfo info;
    struct fb_fix_screeninfo finfo;
    float xdpi;
    float ydpi;
    float fps;

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
        PRIV_FLAGS_FRAMEBUFFER = 0x00000001,
        PRIV_FLAGS_USES_PMEM   = 0x00000002,
    };

    enum {
        LOCK_STATE_WRITE     =   1<<31,
        LOCK_STATE_MAPPED    =   1<<30,
        LOCK_STATE_READ_MASK =   0x3FFFFFFF
    };

    // file-descriptors
    int     magic;
    int     flags;
    int     size;
    int     offset;
    int     base;

    int     pid;

    int     vidNode;
    int     adjustedSize;
    int     pool;
    int     surface;
    int     width;
    int     height;
    int     format;
#if !defined(gcdHARDWARE_2D_COMPOSITION)
    /* 3D composition. */
    int     resolveVidNode;
    int     resolveAdjustedSize;
    int     resolvePool;
    int     resolveSurface;
    int     resolveDoneSignal;
    int     resolveOutstanding;
    int     usage;
    int     private0; /* Padding */
#endif
    /* Out of band */
    int     phys;
    int     fd;

#ifdef __cplusplus

#if !defined(gcdHARDWARE_2D_COMPOSITION)
    /* 3D composition. */
    static const int sNumInts = 23;
    static const int sNumFds  = 0;
#else
    /* 2D hardware composition. */
    static const int sNumInts = 13;
    static const int sNumFds  = 0;
#endif
    static const int sMagic   = 0x3141592;

    private_handle_t(int fd, int size, int flags) :
        magic(sMagic),
        flags(flags),
        size(size),
        offset(0),
        base(0),
        pid(getpid()),
        vidNode(0),
        adjustedSize(0),
        pool(0),
        surface(0),
        width(0),
        height(0),
        format(0),
#if !defined(gcdHARDWARE_2D_COMPOSITION)
        /* 3D composition. */
        resolveVidNode(0),
        resolveAdjustedSize(0),
        resolvePool(0),
        resolveSurface(0),
        resolveDoneSignal(0),
        resolveOutstanding(0),
        usage(0),
#endif
        /* Out of band */
        phys(0),
        fd(fd)

    {
        version = sizeof(native_handle);
        numInts = sNumInts;
        numFds = sNumFds;
    }
    ~private_handle_t() {
        magic = 0;
    }

    bool usesPhysicallyContiguousMemory() {
        return (flags & PRIV_FLAGS_USES_PMEM) != 0;
    }

    static int validate(const native_handle* h) {
        const private_handle_t* hnd = (const private_handle_t*)h;
        if (!h || h->version != sizeof(native_handle) ||
                h->numInts != sNumInts || h->numFds != sNumFds ||
                hnd->magic != sMagic)
        {
            //LOGE("invalid gralloc handle (at %p)", h);
            //LOGE("------------------h=%x", (int)h);
	    //if(h != NULL)
	        //LOGE("h->version=%d, h->numInts=%d, h->numFds=%d, hnd->magic=%x", h->version, h->numInts, h->numFds, hnd->magic);
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
