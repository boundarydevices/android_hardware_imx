/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
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

#include <cutils/ashmem.h>
#include <cutils/atomic.h>
#include <cutils/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#if HAVE_ANDROID_OS
#include <linux/fb.h>
#endif
#include <linux/mxcfb.h>

#include "gr.h"
#include "gralloc_priv.h"

/*****************************************************************************/

// Set TARGET_USE_PAN_DISPLAY to true at compile time if the
// board uses FBIOPAN_DISPLAY to setup page flipping, otherwise
// default ioctl to do page-flipping is FBIOPUT_VSCREENINFO.
#ifndef USE_PAN_DISPLAY
#define USE_PAN_DISPLAY 0
#endif

// numbers of buffers for page flipping
#define NUM_BUFFERS 3
#define EPDC_WAITTIME_MS 300000
#define EPDC_WAITCOUNT 10
#define FB_NAME_PATH "/sys/class/graphics/fb0/name"
#define EPDC_DISPLAY_STR "epdc"

enum { PAGE_FLIP = 0x00000001, LOCKED = 0x00000002 };

struct fb_context_t {
    framebuffer_device_t device;
    bool epdc_display;
};

static bool isEPDCDisplay() {
    char display_name[256];
    int size = 0;
    int display_fd;
    bool ret_val = false;
    memset(display_name, 0, sizeof(display_name));
    display_fd = open(FB_NAME_PATH, O_RDONLY, 0);
    if (display_fd > 0) {
        size = read(display_fd, display_name, sizeof(display_name));
        if ((size > 0) && (strstr(display_name, EPDC_DISPLAY_STR))) {
            ret_val = true;
        }
        close(display_fd);
    }
    return ret_val;
}

__u32 marker_val = 1;
static __u32 update_to_display(private_module_t* m, int left, int top, int width, int height,
                               int wave_mode, int wait_for_complete, uint flags) {
    struct mxcfb_update_data upd_data;
    struct mxcfb_update_marker_data upd_marker_data;
    int retval;
    int wait = wait_for_complete | (flags & EPDC_FLAG_TEST_COLLISION);
    int max_retry = EPDC_WAITCOUNT;

    upd_data.update_mode = UPDATE_MODE_PARTIAL;
    upd_data.waveform_mode = wave_mode;
    upd_data.update_region.left = left;
    upd_data.update_region.width = width;
    upd_data.update_region.top = top;
    upd_data.update_region.height = height;
    upd_data.temp = TEMP_USE_AMBIENT;
    upd_data.flags = flags;

    if (wait) /* Get unique marker value */
        upd_data.update_marker = marker_val++;
    else
        upd_data.update_marker = 0;

    retval = ioctl(m->framebuffer->fd, MXCFB_SEND_UPDATE, &upd_data);
    while (retval < 0) {
        /* We have limited memory available for updates, so wait and
         * then try again after some updates have completed */
        usleep(EPDC_WAITTIME_MS);
        retval = ioctl(m->framebuffer->fd, MXCFB_SEND_UPDATE, &upd_data);
        if (--max_retry <= 0) {
            ALOGE("Max retries exceeded\n");
            wait = 0;
            flags = 0;
            break;
        }
    }

    if (wait) {
        upd_marker_data.update_marker = upd_data.update_marker;

        /* Wait for update to complete */
        // retval = ioctl(fd_fb_ioctl, MXCFB_WAIT_FOR_UPDATE_COMPLETE, &upd_marker_data);
        retval = ioctl(m->framebuffer->fd, MXCFB_WAIT_FOR_UPDATE_COMPLETE, &upd_marker_data);
        if (retval < 0) {
            ALOGE("Wait for update complete failed.  Error = 0x%x", retval);
            flags = 0;
        }
    }

    if (flags & EPDC_FLAG_TEST_COLLISION) {
        ALOGE("Collision test result = %d\n", upd_marker_data.collision_test);
        return upd_marker_data.collision_test;
    } else
        return upd_data.waveform_mode;
}

/*****************************************************************************/

static int fb_setSwapInterval(struct framebuffer_device_t* dev, int interval) {
    fb_context_t* ctx = (fb_context_t*)dev;
    if (interval < dev->minSwapInterval || interval > dev->maxSwapInterval)
        return -EINVAL;
    // FIXME: implement fb_setSwapInterval
    return 0;
}

static int fb_setUpdateRect(struct framebuffer_device_t* dev, int l, int t, int w, int h) {
    if (((w | h) <= 0) || ((l | t) < 0))
        return -EINVAL;

    fb_context_t* ctx = (fb_context_t*)dev;
    private_module_t* m = reinterpret_cast<private_module_t*>(dev->common.module);
    m->info.reserved[0] = 0x54445055; // "UPDT";
    m->info.reserved[1] = (uint16_t)l | ((uint32_t)t << 16);
    m->info.reserved[2] = (uint16_t)(l + w) | ((uint32_t)(t + h) << 16);
    return 0;
}

static int fb_post(struct framebuffer_device_t* dev, buffer_handle_t buffer) {
    if (private_handle_t::validate(buffer) < 0)
        return -EINVAL;

    fb_context_t* ctx = (fb_context_t*)dev;

    private_handle_t const* hnd = reinterpret_cast<private_handle_t const*>(buffer);
    private_module_t* m = reinterpret_cast<private_module_t*>(dev->common.module);
    if (hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER) {
        int wave_mode = WAVEFORM_MODE_AUTO;
        const size_t offset = hnd->base - m->framebuffer->base;
        m->info.activate = FB_ACTIVATE_VBL;
        m->info.yoffset = offset / m->finfo.line_length;
        if (ioctl(m->framebuffer->fd, FBIOPAN_DISPLAY, &m->info) == -1) {
            ALOGE("FBIOPUT_VSCREENINFO failed");
            m->base.unlock(&m->base, buffer);
            return -errno;
        }

        // Update EPDC with mode, and waveform setting
        if (ctx->epdc_display == true)
            update_to_display(m, 0, 0, m->info.xres, m->info.yres, WAVEFORM_MODE_AUTO, 1, 0);

        m->currentBuffer = buffer;

    } else {
        // If we can't do the page_flip, just copy the buffer to the front
        // FIXME: use copybit HAL instead of memcpy

        void* fb_vaddr;
        void* buffer_vaddr;

        m->base.lock(&m->base, m->framebuffer, GRALLOC_USAGE_SW_WRITE_RARELY, 0, 0, m->info.xres,
                     m->info.yres, &fb_vaddr);

        m->base.lock(&m->base, buffer, GRALLOC_USAGE_SW_READ_RARELY, 0, 0, m->info.xres,
                     m->info.yres, &buffer_vaddr);

        memcpy(fb_vaddr, buffer_vaddr, m->finfo.line_length * m->info.yres);

        m->base.unlock(&m->base, buffer);
        m->base.unlock(&m->base, m->framebuffer);
    }

    return 0;
}

/*****************************************************************************/

int mapFrameBufferLocked(struct private_module_t* module) {
    // already initialized...
    if (module->framebuffer) {
        return 0;
    }

    char const* const device_template[] = {"/dev/graphics/fb%u", "/dev/fb%u", 0};

    int fd = -1;
    int i = 0;
    char name[64];

    while ((fd == -1) && device_template[i]) {
        snprintf(name, 64, device_template[i], 0);
        fd = open(name, O_RDWR, 0);
        i++;
    }
    if (fd < 0)
        return -errno;

    struct fb_fix_screeninfo finfo;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) == -1)
        return -errno;

    struct fb_var_screeninfo info;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &info) == -1)
        return -errno;

    info.reserved[0] = 0;
    info.reserved[1] = 0;
    info.reserved[2] = 0;
    info.xoffset = 0;
    info.yoffset = 0;
    info.activate = FB_ACTIVATE_NOW;

    /*
     * Request NUM_BUFFERS screens (at lest 2 for page flipping)
     */
    info.yres_virtual = info.yres * NUM_BUFFERS;

    uint32_t flags = PAGE_FLIP;
#if USE_PAN_DISPLAY
    if (ioctl(fd, FBIOPAN_DISPLAY, &info) == -1) {
        ALOGW("FBIOPAN_DISPLAY failed, page flipping not supported");
#else
    if (ioctl(fd, FBIOPUT_VSCREENINFO, &info) == -1) {
        ALOGW("FBIOPUT_VSCREENINFO failed, page flipping not supported");
#endif
        info.yres_virtual = info.yres;
        flags &= ~PAGE_FLIP;
    }

    if (info.yres_virtual < info.yres * 2) {
        // we need at least 2 for page-flipping
        info.yres_virtual = info.yres;
        flags &= ~PAGE_FLIP;
        ALOGW("page flipping not supported (yres_virtual=%d, requested=%d)", info.yres_virtual,
              info.yres * 2);
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &info) == -1)
        return -errno;

    uint64_t refreshQuotient =
            (uint64_t(info.upper_margin + info.lower_margin + info.yres + info.vsync_len) *
             (info.left_margin + info.right_margin + info.xres + info.hsync_len) * info.pixclock);

    /* Beware, info.pixclock might be 0 under emulation, so avoid a
     * division-by-0 here (SIGFPE on ARM) */
    int refreshRate = refreshQuotient > 0 ? (int)(1000000000000000LLU / refreshQuotient) : 0;

    if (refreshRate == 0) {
        // bleagh, bad info from the driver
        refreshRate = 60 * 1000; // 60 Hz
    }

    // epdc panel have a variable refresh rate from 2~30HZ
    if (isEPDCDisplay())
        refreshRate = 30 * 1000;

    if (int(info.width) <= 0 || int(info.height) <= 0) {
        // the driver doesn't return that information
        // default to 160 dpi
        info.width = ((info.xres * 25.4f) / 160.0f + 0.5f);
        info.height = ((info.yres * 25.4f) / 160.0f + 0.5f);
    }

    float xdpi = (info.xres * 25.4f) / info.width;
    float ydpi = (info.yres * 25.4f) / info.height;
    float fps = refreshRate / 1000.0f;

    ALOGI("using (fd=%d)\n"
          "id           = %s\n"
          "xres         = %d px\n"
          "yres         = %d px\n"
          "xres_virtual = %d px\n"
          "yres_virtual = %d px\n"
          "bpp          = %d\n"
          "r            = %2u:%u\n"
          "g            = %2u:%u\n"
          "b            = %2u:%u\n",
          fd, finfo.id, info.xres, info.yres, info.xres_virtual, info.yres_virtual,
          info.bits_per_pixel, info.red.offset, info.red.length, info.green.offset,
          info.green.length, info.blue.offset, info.blue.length);

    ALOGI("width        = %d mm (%f dpi)\n"
          "height       = %d mm (%f dpi)\n"
          "refresh rate = %.2f Hz\n",
          info.width, xdpi, info.height, ydpi, fps);

    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) == -1)
        return -errno;

    if (finfo.smem_len <= 0)
        return -errno;

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
    module->framebuffer = new private_handle_t(dup(fd), fbSize, 0);

    module->numBuffers = info.yres_virtual / info.yres;
    module->bufferMask = 0;

    void* vaddr = mmap(0, fbSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (vaddr == MAP_FAILED) {
        ALOGE("Error mapping the framebuffer (%s)", strerror(errno));
        return -errno;
    }
    module->framebuffer->base = intptr_t(vaddr);
    memset(vaddr, 0, fbSize);
    return 0;
}

static int mapFrameBuffer(struct private_module_t* module) {
    pthread_mutex_lock(&module->lock);
    int err = mapFrameBufferLocked(module);
    pthread_mutex_unlock(&module->lock);
    return err;
}

/*****************************************************************************/

static int fb_close(struct hw_device_t* dev) {
    fb_context_t* ctx = (fb_context_t*)dev;
    if (ctx) {
        free(ctx);
    }
    return 0;
}

int fb_device_open(hw_module_t const* module, const char* name, hw_device_t** device) {
    int status = -EINVAL;
    if (!strcmp(name, GRALLOC_HARDWARE_FB0)) {
        /* initialize our state here */
        fb_context_t* dev = (fb_context_t*)malloc(sizeof(*dev));
        memset(dev, 0, sizeof(*dev));

        /* initialize the procs */
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.version = 0;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = fb_close;
        dev->device.setSwapInterval = fb_setSwapInterval;
        dev->device.post = fb_post;
        dev->device.setUpdateRect = 0;

        if (isEPDCDisplay())
            dev->epdc_display = true;

        private_module_t* m = (private_module_t*)module;
        status = mapFrameBuffer(m);
        if (status >= 0) {
            int stride = m->finfo.line_length / (m->info.bits_per_pixel >> 3);
            int format = (m->info.bits_per_pixel == 32)
                    ? (m->info.red.offset ? HAL_PIXEL_FORMAT_BGRA_8888 : HAL_PIXEL_FORMAT_RGBX_8888)
                    : HAL_PIXEL_FORMAT_RGB_565;
            const_cast<uint32_t&>(dev->device.flags) = 0;
            const_cast<uint32_t&>(dev->device.width) = m->info.xres;
            const_cast<uint32_t&>(dev->device.height) = m->info.yres;
            const_cast<int&>(dev->device.stride) = stride;
            const_cast<int&>(dev->device.format) = format;
            const_cast<float&>(dev->device.xdpi) = m->xdpi;
            const_cast<float&>(dev->device.ydpi) = m->ydpi;
            const_cast<float&>(dev->device.fps) = m->fps;
            const_cast<int&>(dev->device.minSwapInterval) = 1;
            const_cast<int&>(dev->device.maxSwapInterval) = 1;
            *device = &dev->device.common;
        }
    }
    return status;
}
