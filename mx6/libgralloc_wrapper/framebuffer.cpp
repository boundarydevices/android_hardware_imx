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

#include <sys/mman.h>

#include <dlfcn.h>

#include <cutils/ashmem.h>
#include <cutils/log.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>

#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>

#include <cutils/log.h>
#include <cutils/atomic.h>
#include <cutils/properties.h>

#if HAVE_ANDROID_OS
#include <linux/fb.h>
#include <linux/mxcfb.h>
#include <linux/videodev.h>
#include <sys/mman.h>

#include <linux/ipu.h>

#endif
#include <GLES/gl.h>
#include <pthread.h>
#include <semaphore.h>

#include <utils/String8.h>
#include "gralloc_priv.h"
/*****************************************************************************/

// numbers of buffers for page flipping
#define NUM_BUFFERS 3

inline size_t roundUpToPageSize(size_t x) {
    return (x + (PAGE_SIZE-1)) & ~(PAGE_SIZE-1);
}

enum {
    PAGE_FLIP = 0x00000001,
    LOCKED = 0x00000002
};

struct fb_context_t {
    framebuffer_device_t  device;
    int mainDisp_fd;
    private_module_t* priv_m;
    int isMainDisp;
};

static int nr_framebuffers;

/*****************************************************************************/

static int fb_setSwapInterval(struct framebuffer_device_t* dev,
            int interval)
{
    fb_context_t* ctx = (fb_context_t*)dev;
    if (interval < dev->minSwapInterval || interval > dev->maxSwapInterval)
        return -EINVAL;
    // FIXME: implement fb_setSwapInterval
    return 0;
}

static int fb_setUpdateRect(struct framebuffer_device_t* dev,
        int l, int t, int w, int h)
{
    if (((w|h) <= 0) || ((l|t)<0))
        return -EINVAL;

    fb_context_t* ctx = (fb_context_t*)dev;
    private_module_t* m = reinterpret_cast<private_module_t*>(
            dev->common.module);
    m->info.reserved[0] = 0x54445055; // "UPDT";
    m->info.reserved[1] = (uint16_t)l | ((uint32_t)t << 16);
    m->info.reserved[2] = (uint16_t)(l+w) | ((uint32_t)(t+h) << 16);
    return 0;
}

static int fb_post(struct framebuffer_device_t* dev, buffer_handle_t buffer)
{
    if (!buffer)
        return -EINVAL;

    fb_context_t* ctx = (fb_context_t*)dev;

    private_handle_t const* hnd = reinterpret_cast<private_handle_t const*>(buffer);
    private_module_t* m = reinterpret_cast<private_module_t*>(
            dev->common.module);
    if (m->currentBuffer) {
        m->base.unlock(&m->base, m->currentBuffer);
        m->currentBuffer = 0;
    }

    if (hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER) {

        void *vaddr = NULL;
        m->base.lock(&m->base, buffer, 
                private_module_t::PRIV_USAGE_LOCKED_FOR_POST, 
                0, 0, ALIGN_PIXEL(m->info.xres), ALIGN_PIXEL_128(m->info.yres), &vaddr);

        const size_t offset = hnd->base - m->framebuffer->base;
        m->info.activate = FB_ACTIVATE_VBL;
        m->info.yoffset = offset / m->finfo.line_length;

        if (ioctl(m->framebuffer->fd, FBIOPAN_DISPLAY, &m->info) == -1) {
            ALOGW("FBIOPAN_DISPLAY failed: %s", strerror(errno));
            m->currentBuffer = buffer;
            return 0;
            //return -errno;
        }

        m->currentBuffer = buffer;
        
    } else {
        // If we can't do the page_flip, just copy the buffer to the front 
        // FIXME: use copybit HAL instead of memcpy
        
        void* fb_vaddr;
        void* buffer_vaddr;
        
        m->base.lock(&m->base, m->framebuffer, 
                GRALLOC_USAGE_SW_WRITE_RARELY, 
                0, 0, ALIGN_PIXEL(m->info.xres), ALIGN_PIXEL_128(m->info.yres),
                &fb_vaddr);

        m->base.lock(&m->base, buffer, 
                GRALLOC_USAGE_SW_READ_RARELY, 
                0, 0, ALIGN_PIXEL(m->info.xres), ALIGN_PIXEL_128(m->info.yres),
                &buffer_vaddr);

        memcpy(fb_vaddr, buffer_vaddr, m->finfo.line_length * ALIGN_PIXEL_128(m->info.yres));

        m->base.unlock(&m->base, buffer); 
        m->base.unlock(&m->base, m->framebuffer); 
    }
    
    return 0;
}

static int fb_compositionComplete(struct framebuffer_device_t* dev)
{
  //  glFinish();
    return 0;
}

/*****************************************************************************/
static int mapFrameBufferWithFbid(struct private_module_t* module, int fbid)
{
    // already initialized...
    if (module->framebuffer) {
        return 0;
    }
        
    char const * const device_template[] = {
            "/dev/graphics/fb%u",
            "/dev/fb%u",
            0 };

    int fd = -1;
    int i=0;
    char name[64];

    while ((fd==-1) && device_template[i]) {
        snprintf(name, 64, device_template[i], fbid);
        fd = open(name, O_RDWR, 0);
        i++;
    }
    if (fd < 0) {
        ALOGE("<%s,%d> open %s failed", __FUNCTION__, __LINE__, name);
        return -errno;
    }

    if(fbid != 0) {
        int blank = FB_BLANK_UNBLANK;
        if(ioctl(fd, FBIOBLANK, blank) < 0) {
            ALOGE("<%s, %d> ioctl FBIOBLANK failed", __FUNCTION__, __LINE__);
        }
    }

    struct fb_fix_screeninfo finfo;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        ALOGE("<%s,%d> FBIOGET_FSCREENINFO failed", __FUNCTION__, __LINE__);
        return -errno;
    }

    struct fb_var_screeninfo info;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &info) == -1) {
        ALOGE("<%s,%d> FBIOGET_VSCREENINFO failed", __FUNCTION__, __LINE__);
        return -errno;
    }

    info.reserved[0] = 0;
    info.reserved[1] = 0;
    info.reserved[2] = 0;
    info.xoffset = 0;
    info.yoffset = 0;
    info.activate = FB_ACTIVATE_NOW;

    if(info.bits_per_pixel == 32){
        ALOGW("32bpp setting of Framebuffer catched!");
        /*
         * Explicitly request RGBA 8/8/8/8
         */
        info.red.offset       = 0;
        info.red.length       = 8;
        info.red.msb_right    = 0;
        info.green.offset     = 8;
        info.green.length     = 8;
        info.green.msb_right  = 0;
        info.blue.offset      = 16;
        info.blue.length      = 8;
        info.blue.msb_right   = 0;
        info.transp.offset    = 24;
        info.transp.length    = 8;
        info.transp.msb_right = 0;
    }
    else{
        /*
         * Explicitly request 5/6/5
         */
        info.bits_per_pixel   = 16;
        info.red.offset       = 11;
        info.red.length       = 5;
        info.red.msb_right    = 0;
        info.green.offset     = 5;
        info.green.length     = 6;
        info.green.msb_right  = 0;
        info.blue.offset      = 0;
        info.blue.length      = 5;
        info.blue.msb_right   = 0;
        info.transp.offset    = 0;
        info.transp.length    = 0;
        info.transp.msb_right = 0;
    }
    /*
     * Request nr_framebuffers screens (at lest 2 for page flipping)
     */
    info.yres_virtual = ALIGN_PIXEL_128(info.yres) * nr_framebuffers;
    info.xres_virtual = ALIGN_PIXEL(info.xres);
    
    uint32_t flags = PAGE_FLIP;
    if (ioctl(fd, FBIOPUT_VSCREENINFO, &info) == -1) {
        info.yres_virtual = ALIGN_PIXEL_128(info.yres);
        flags &= ~PAGE_FLIP;
        ALOGW("FBIOPUT_VSCREENINFO failed, page flipping not supported");
    }

    if (info.yres_virtual < ALIGN_PIXEL_128(info.yres) * 2) {
        // we need at least 2 for page-flipping
        info.yres_virtual = ALIGN_PIXEL_128(info.yres);
        flags &= ~PAGE_FLIP;
        ALOGW("page flipping not supported (yres_virtual=%d, requested=%d)",
                info.yres_virtual, ALIGN_PIXEL_128(info.yres)*2);
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &info) == -1) {
        ALOGE("<%s,%d> FBIOGET_VSCREENINFO failed", __FUNCTION__, __LINE__);
        return -errno;
    }

    int refreshRate = 1000000000000000LLU /
    (
            uint64_t(info.upper_margin + info.lower_margin + info.yres + info.vsync_len)
            * (info.left_margin  + info.right_margin + info.xres + info.hsync_len)
            * info.pixclock
    );

    if (refreshRate == 0) {
        // bleagh, bad info from the driver
        refreshRate = 60*1000;  // 60 Hz
    }

    if (int(info.width) <= 0 || int(info.height) <= 0) {
        // the driver doesn't return that information
        // default to 160 dpi
        info.width  = ((info.xres * 25.4f)/160.0f + 0.5f);
        info.height = ((info.yres * 25.4f)/160.0f + 0.5f);
    }

    float xdpi = (info.xres * 25.4f) / info.width;
    float ydpi = (info.yres * 25.4f) / info.height;
    float fps  = refreshRate / 1000.0f;

    ALOGW(   "using (fd=%d)\n"
            "id           = %s\n"
            "xres         = %d px\n"
            "yres         = %d px\n"
            "xres_virtual = %d px\n"
            "yres_virtual = %d px\n"
            "bpp          = %d\n"
            "r            = %2u:%u\n"
            "g            = %2u:%u\n"
            "b            = %2u:%u\n",
            fd,
            finfo.id,
            info.xres,
            info.yres,
            info.xres_virtual,
            info.yres_virtual,
            info.bits_per_pixel,
            info.red.offset, info.red.length,
            info.green.offset, info.green.length,
            info.blue.offset, info.blue.length
    );

    ALOGW(   "width        = %d mm (%f dpi)\n"
            "height       = %d mm (%f dpi)\n"
            "refresh rate = %.2f Hz\n",
            info.width,  xdpi,
            info.height, ydpi,
            fps
    );


    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        ALOGE("<%s,%d> FBIOGET_FSCREENINFO failed", __FUNCTION__, __LINE__);
        return -errno;
    }

    if (finfo.smem_len <= 0) {
        ALOGE("<%s,%d> finfo.smem_len <= 0", __FUNCTION__, __LINE__);
        return -errno;
    }


    module->flags = flags;
    module->info = info;
    module->finfo = finfo;
    module->xdpi = xdpi;
    module->ydpi = ydpi;
    module->fps = fps;

    /*
     * map the framebuffer
     */

    int err;
    size_t fbSize = roundUpToPageSize(finfo.line_length * info.yres_virtual);
    module->framebuffer = new private_handle_t(fd, fbSize,
            private_handle_t::PRIV_FLAGS_USES_DRV);

    module->numBuffers = info.yres_virtual / ALIGN_PIXEL_128(info.yres);
    module->bufferMask = 0;

    void* vaddr = mmap(0, fbSize, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (vaddr == MAP_FAILED) {
        ALOGE("Error mapping the framebuffer (%s)", strerror(errno));
        return -errno;
    }
    module->framebuffer->base = intptr_t(vaddr);
    module->framebuffer->phys = intptr_t(finfo.smem_start);
    memset(vaddr, 0, fbSize);

    return 0;
}

int mapFrameBufferLocked(struct private_module_t* module)
{
    return mapFrameBufferWithFbid(module, 0);
}

static int mapFrameBuffer(struct private_module_t* module)
{
    pthread_mutex_lock(&module->lock);
    int err = mapFrameBufferLocked(module);
    pthread_mutex_unlock(&module->lock);
    return err;
}

static int unMapFrameBuffer(fb_context_t* ctx, struct private_module_t* module)
{
    pthread_mutex_lock(&module->lock);

    size_t fbSize = module->framebuffer->size;
    int fd = module->framebuffer->fd;
    void* addr = (void*)(module->framebuffer->base);
    munmap(addr, fbSize);
    delete (module->framebuffer);
    module->framebuffer = NULL;
    module->closeDevice = true;

    close(fd);
    pthread_mutex_unlock(&module->lock);

    return 0;
}
/*****************************************************************************/

static int fb_close(struct hw_device_t *dev)
{
    fb_context_t* ctx = (fb_context_t*)dev;
    if(ctx) {
        if (!ctx->isMainDisp) {
            unMapFrameBuffer(ctx, ctx->priv_m);
        }

        free(ctx);
    }

    return 0;
}

static void fb_device_init(private_module_t* m, fb_context_t *dev)
{
    int stride = m->finfo.line_length / (m->info.bits_per_pixel >> 3);
    const_cast<uint32_t&>(dev->device.flags) = 0xfb0;
    const_cast<uint32_t&>(dev->device.width) = m->info.xres;
    const_cast<uint32_t&>(dev->device.height) = m->info.yres;
    const_cast<int&>(dev->device.stride) = stride;
    if(m->info.bits_per_pixel != 32) {
	const_cast<int&>(dev->device.format) = HAL_PIXEL_FORMAT_RGB_565;
    }
    else{
        if (m->info.red.offset == 0) {
	    const_cast<int&>(dev->device.format) = HAL_PIXEL_FORMAT_RGBA_8888;
        }
        else {
	    const_cast<int&>(dev->device.format) = HAL_PIXEL_FORMAT_BGRA_8888;
        }
    }
    const_cast<float&>(dev->device.xdpi) = m->xdpi;
    const_cast<float&>(dev->device.ydpi) = m->ydpi;
    const_cast<float&>(dev->device.fps) = m->fps;
    const_cast<int&>(dev->device.minSwapInterval) = 1;
    const_cast<int&>(dev->device.maxSwapInterval) = 1;
    const_cast<int &>(dev->device.numFramebuffers) = NUM_BUFFERS;

}

int fb_device_open(hw_module_t const* module, const char* name,
        hw_device_t** device)
{
    int status = 0;
    char value[PROPERTY_VALUE_MAX];
    if (!strncmp(name, GRALLOC_HARDWARE_FB, 2)) {
        framebuffer_device_t *fbdev;
        nr_framebuffers = NUM_BUFFERS;
        property_get("ro.product.device", value, "");
        if (0 == strcmp(value, "imx50_rdp")) {
            nr_framebuffers = 2;
        }

        /* initialize our state here */
        fb_context_t *dev = (fb_context_t*)malloc(sizeof(*dev));
        memset(dev, 0, sizeof(*dev));

        /* initialize the procs */
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.version = 0;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = fb_close;
        dev->device.setSwapInterval = fb_setSwapInterval;
        dev->device.post            = fb_post;
        dev->device.setUpdateRect = 0;
        dev->device.compositionComplete = fb_compositionComplete;

        if (!strcmp(name, GRALLOC_HARDWARE_FB0)) {
            dev->device.common.module = const_cast<hw_module_t*>(module);
            private_module_t* m = (private_module_t*)module;
            status = mapFrameBuffer(m);
            if (status >= 0) {
                fb_device_init(m, dev);
            }

            dev->priv_m = m;
            dev->mainDisp_fd = m->framebuffer->fd;
            dev->isMainDisp = 1;
        } else {
            private_module_t* orig_m = (private_module_t*)module;
            private_module_t* priv_m = NULL;
            if (orig_m->external_module == NULL) {
                priv_m = (private_module_t*)malloc(sizeof(*priv_m));
                memset(priv_m, 0, sizeof(*priv_m));
                memcpy(priv_m, orig_m, sizeof(*priv_m));

                orig_m->external_module = priv_m;
            }
            else {
                priv_m = orig_m->external_module;
                priv_m->closeDevice = false;
            }

            dev->device.common.module = (hw_module_t*)(priv_m);
            priv_m->framebuffer = NULL;
            int fbid = (int)*device;
            status = mapFrameBufferWithFbid(priv_m, fbid);
            if (status >= 0) {
                fb_device_init(priv_m, dev);
            }

            dev->priv_m = priv_m;
            dev->mainDisp_fd = orig_m->framebuffer->fd;
            dev->isMainDisp = 0;

            gralloc_context_t* gra_dev = (gralloc_context_t*)orig_m->priv_dev;
            alloc_device_t *ext = gra_dev->ext_dev;
            ext->common.module = (hw_module_t*)(priv_m);
        }

        *device = &dev->device.common;
        fbdev = (framebuffer_device_t*) *device;
        fbdev->reserved[0] = nr_framebuffers;
    } 

    return status;
}
