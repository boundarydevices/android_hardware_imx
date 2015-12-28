/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2010-2015 Freescale Semiconductor, Inc.
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
#include <gralloc_priv.h>
#include <BufferManager.h>
/*****************************************************************************/

// numbers of buffers for page flipping
#ifndef NUM_FRAMEBUFFER_SURFACE_BUFFERS
#define NUM_BUFFERS 3
#else
#define NUM_BUFFERS NUM_FRAMEBUFFER_SURFACE_BUFFERS
#endif

inline size_t roundUpToPageSize(size_t x) {
    return (x + (PAGE_SIZE-1)) & ~(PAGE_SIZE-1);
}

enum {
    PAGE_FLIP = 0x00000001,
    LOCKED = 0x00000002
};

struct fb_context_t {
    framebuffer_device_t  device;
    Display* display;
    int isMainDisp;
};

static int nr_framebuffers;

/*****************************************************************************/

int Display::setSwapInterval(struct framebuffer_device_t* dev,
            int interval)
{
    fb_context_t* ctx = (fb_context_t*)dev;
    if (interval < dev->minSwapInterval || interval > dev->maxSwapInterval)
        return -EINVAL;
    // FIXME: implement fb_setSwapInterval
    return 0;
}

int Display::setUpdateRect(struct framebuffer_device_t* dev,
        int l, int t, int w, int h)
{
    if (((w|h) <= 0) || ((l|t)<0))
        return -EINVAL;

    fb_context_t* ctx = (fb_context_t*)dev;
    Display* m = ctx->display;
    m->mInfo.reserved[0] = 0x54445055; // "UPDT";
    m->mInfo.reserved[1] = (uint16_t)l | ((uint32_t)t << 16);
    m->mInfo.reserved[2] = (uint16_t)(l+w) | ((uint32_t)(t+h) << 16);
    return 0;
}

int Display::postBuffer(struct framebuffer_device_t* dev, buffer_handle_t buffer)
{
    if (!buffer || !dev) {
        ALOGE("%s invalid parameters", __FUNCTION__);
        return -EINVAL;
    }

    fb_context_t* ctx = (fb_context_t*)dev;

    private_handle_t const* hnd = reinterpret_cast<
                                  private_handle_t const*>(buffer);
    BufferManager* m = BufferManager::getInstance();
    if (m == NULL) {
        ALOGE("%s cat't get buffer manager", __FUNCTION__);
        return -EINVAL;
    }

    Display* display = ctx->display;
    if (display->mCurrentBuffer) {
        m->unlock(display->mCurrentBuffer);
        display->mCurrentBuffer = NULL;
    }

    if (hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER) {

        void *vaddr = NULL;
        m->lock(buffer,
                private_module_t::PRIV_USAGE_LOCKED_FOR_POST,
                0, 0, ALIGN_PIXEL_16(display->mInfo.xres),
                ALIGN_PIXEL_16(display->mInfo.yres), &vaddr);

        const size_t offset = hnd->base - display->mFramebuffer->base;
        display->mInfo.activate = FB_ACTIVATE_VBL;
        display->mInfo.yoffset = offset / display->mFinfo.line_length;

        if (ioctl(display->mFramebuffer->fd, FBIOPAN_DISPLAY, &display->mInfo) == -1) {
            ALOGW("FBIOPAN_DISPLAY failed: %s", strerror(errno));
            display->mCurrentBuffer = buffer;
            return 0;
            //return -errno;
        }

        display->mCurrentBuffer = buffer;

    } else {
        // If we can't do the page_flip, just copy the buffer to the front
        // FIXME: use copybit HAL instead of memcpy

        void* fb_vaddr;
        void* buffer_vaddr;

        m->lock(display->mFramebuffer,
                GRALLOC_USAGE_SW_WRITE_RARELY,
                0, 0, ALIGN_PIXEL_16(display->mInfo.xres),
                 ALIGN_PIXEL_16(display->mInfo.yres),
                &fb_vaddr);

        m->lock(buffer,
                GRALLOC_USAGE_SW_READ_RARELY,
                0, 0, ALIGN_PIXEL_16(display->mInfo.xres),
                 ALIGN_PIXEL_16(display->mInfo.yres),
                &buffer_vaddr);

        memcpy(fb_vaddr, buffer_vaddr,
        display->mFinfo.line_length * ALIGN_PIXEL_16(display->mInfo.yres));

        m->unlock(buffer);
        m->unlock(display->mFramebuffer);
    }

    return 0;
}

int Display::compositionComplete(struct framebuffer_device_t* /*dev*/)
{
  //  glFinish();
    return 0;
}

int Display::checkFramebufferFormat(int fd, uint32_t &flags)
{
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
    /*
     * Request nr_framebuffers screens (at lest 2 for page flipping)
     */
    info.yres_virtual = ALIGN_PIXEL_16(info.yres) * nr_framebuffers;
    /*
     *note: 16 alignment here should align with BufferManager::alloc.
     */
    info.xres_virtual = ALIGN_PIXEL_16(info.xres);

    if (info.bits_per_pixel == 32) {
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

        if (ioctl(fd, FBIOPUT_VSCREENINFO, &info) == -1) {
            flags &= ~PAGE_FLIP;
            ALOGW("FBIOPUT_VSCREENINFO failed, page flipping not supported");
        }

        if (ioctl(fd, FBIOGET_VSCREENINFO, &info) == -1) {
            ALOGE("<%s,%d> FBIOGET_VSCREENINFO failed", __FUNCTION__, __LINE__);
            return -errno;
        }
        if (info.red.offset != 0 || info.red.length != 8 ||
            info.green.offset != 8 || info.green.length != 8 ||
            info.blue.offset != 16 || info.blue.length != 8) {
            /*
             * Explicitly request BGRA 8/8/8/8
             */
            info.red.offset       = 16;
            info.red.length       = 8;
            info.red.msb_right    = 0;
            info.green.offset     = 8;
            info.green.length     = 8;
            info.green.msb_right  = 0;
            info.blue.offset      = 0;
            info.blue.length      = 8;
            info.blue.msb_right   = 0;
            info.transp.offset    = 24;
            info.transp.length    = 8;
            info.transp.msb_right = 0;

            if (ioctl(fd, FBIOPUT_VSCREENINFO, &info) == -1) {
                flags &= ~PAGE_FLIP;
                ALOGW("FBIOPUT_VSCREENINFO failed, page flipping not supported");
            }

            if (ioctl(fd, FBIOGET_VSCREENINFO, &info) == -1) {
                ALOGE("<%s,%d> FBIOGET_VSCREENINFO failed", __FUNCTION__, __LINE__);
                return -errno;
            }
            if (info.red.offset != 16 || info.red.length != 8 ||
                info.green.offset != 8 || info.green.length != 8 ||
                info.blue.offset != 0 || info.blue.length != 8) {
                ALOGE("display doesn't support RGBA8888 and BRGA8888 in 32bpp,"
                       "which are supported in framework."
                       "please configure 16bpp in commandline to have a try");
                return -errno;
            }
            ALOGI("32bpp setting of Framebuffer with BGRA8888 format!");
        }
        else {
            ALOGI("32bpp setting of Framebuffer with RGBA8888 format!");
        }
    }
    else {
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

        if (ioctl(fd, FBIOPUT_VSCREENINFO, &info) == -1) {
            flags &= ~PAGE_FLIP;
            ALOGW("FBIOPUT_VSCREENINFO failed, page flipping not supported");
        }

        if (ioctl(fd, FBIOGET_VSCREENINFO, &info) == -1) {
            ALOGE("<%s,%d> FBIOGET_VSCREENINFO failed", __FUNCTION__, __LINE__);
            return -errno;
        }
        if (info.red.offset != 11 || info.red.length != 5 ||
            info.green.offset != 5 || info.green.length != 6 ||
            info.blue.offset != 0 || info.blue.length != 5) {
            ALOGE("display doesn't support RGB565 in 16bpp,"
                   "which is only support in framework");
            return -errno;
        }

        ALOGI("16bpp setting of Framebuffer with RGB565 format!");
    }

    if (info.yres_virtual < ALIGN_PIXEL_16(info.yres) * 2) {
        // we need at least 2 for page-flipping
        flags &= ~PAGE_FLIP;
        ALOGW("page flipping not supported (yres_virtual=%d, requested=%d)",
                info.yres_virtual, ALIGN_PIXEL_16(info.yres)*2);
    }

    return 0;
}

/*****************************************************************************/
int Display::initialize(int fb)
{
    Mutex::Autolock _l(mLock);

    fb_num = fb;
    // already initialized...
    if (mFramebuffer != NULL) {
        ALOGI("display already initialized...");
        return 0;
    }

    int fbid = fb_num;
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

    uint32_t flags = PAGE_FLIP;
    if (checkFramebufferFormat(fd, flags) != 0) {
        ALOGE("<%s,%d> checkFramebufferFormat failed", __FUNCTION__, __LINE__);
        return -errno;
    }

    struct fb_var_screeninfo info;
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

    mInfo = info;
    mFinfo = finfo;
    mXdpi = xdpi;
    mYdpi = ydpi;
    mFps = fps;

    /*
     * map the framebuffer
     */

    int err;
    size_t fbSize = roundUpToPageSize(finfo.line_length * info.yres_virtual);
    mFramebuffer = new private_handle_t(fd, fbSize,
            private_handle_t::PRIV_FLAGS_FRAMEBUFFER);

    mNumBuffers = info.yres_virtual / ALIGN_PIXEL_16(info.yres);
    mBufferMask = 0;

    void* vaddr = mmap(0, fbSize, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (vaddr == MAP_FAILED) {
        ALOGE("Error mapping the framebuffer (%s)", strerror(errno));
        return -errno;
    }
    mFramebuffer->base = intptr_t(vaddr);
    mFramebuffer->phys = intptr_t(finfo.smem_start);
    memset(vaddr, 0, fbSize);

    return 0;
}

int Display::uninitialize()
{
    Mutex::Autolock _l(mLock);

    size_t fbSize = mFramebuffer->size;
    int fd = mFramebuffer->fd;
    // unmap framebuffer should be done at gralloc_free;
    //void* addr = (void*)(module->framebuffer->base);
    //munmap(addr, fbSize);
    delete (mFramebuffer);
    mFramebuffer = NULL;
    mBufferMask = 0;

    fb_num = -1;
    close(fd);

    return 0;
}
/*****************************************************************************/

int Display::closeDevice(struct hw_device_t *dev)
{
    fb_context_t* ctx = (fb_context_t*)dev;
    if(ctx) {
        if (!ctx->isMainDisp && ctx->display != NULL) {
            ctx->display->uninitialize();
        }
        ctx->display = NULL;
        free(ctx);
    }

    return 0;
}

void Display::setContext(fb_context_t *dev)
{
    int stride = mFinfo.line_length / (mInfo.bits_per_pixel >> 3);
    const_cast<uint32_t&>(dev->device.flags) = 0xfb0;
    const_cast<uint32_t&>(dev->device.width) = mInfo.xres;
    const_cast<uint32_t&>(dev->device.height) = mInfo.yres;
    const_cast<int&>(dev->device.stride) = stride;
    if(mInfo.bits_per_pixel != 32) {
	    const_cast<int&>(dev->device.format) = HAL_PIXEL_FORMAT_RGB_565;
    }
    else{
        if (mInfo.red.offset == 0) {
	        const_cast<int&>(dev->device.format) = HAL_PIXEL_FORMAT_RGBA_8888;
        }
        else {
	        const_cast<int&>(dev->device.format) = HAL_PIXEL_FORMAT_BGRA_8888;
        }
    }
    const_cast<float&>(dev->device.xdpi) = mXdpi;
    const_cast<float&>(dev->device.ydpi) = mYdpi;
    const_cast<float&>(dev->device.fps) = mFps;
    const_cast<int&>(dev->device.minSwapInterval) = 1;
    const_cast<int&>(dev->device.maxSwapInterval) = 1;
    const_cast<int &>(dev->device.numFramebuffers) = NUM_BUFFERS;
}

int Display::allocFrameBuffer(size_t size, int usage, buffer_handle_t* pHandle)
{
    Mutex::Autolock _l(mLock);

    if (mFramebuffer == NULL) {
        ALOGE("%s frame buffer device not opened", __FUNCTION__);
        return -EINVAL;
    }

    const uint32_t bufferMask = mBufferMask;
    const uint32_t numBuffers = mNumBuffers;
    const size_t bufferSize = mFinfo.line_length * ALIGN_PIXEL_16(mInfo.yres);
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
    intptr_t vaddr = intptr_t(mFramebuffer->base);

    BufferManager* manager = BufferManager::getInstance();
    if (manager == NULL) {
        ALOGE("%s cat't get buffer manager", __FUNCTION__);
        return -EINVAL;
    }
    private_handle_t* hnd = manager->createPrivateHandle(mFramebuffer->fd,
            size, private_handle_t::PRIV_FLAGS_FRAMEBUFFER);

    // find a free slot
    for (uint32_t i=0 ; i<numBuffers ; i++) {
        if ((bufferMask & (1LU<<i)) == 0) {
            mBufferMask |= (1LU<<i);
            break;
        }
        vaddr += bufferSize;
    }

    if (usage & GRALLOC_USAGE_HW_FBX) {
        hnd->flags |= private_handle_t::PRIV_FLAGS_FRAMEBUFFER_X;
    }
    /*else if (usage & GRALLOC_USAGE_HW_FB2X) {
        hnd->flags |= private_handle_t::PRIV_FLAGS_FRAMEBUFFER_2X;
    }*/

    hnd->base = vaddr;
    hnd->offset = vaddr - intptr_t(mFramebuffer->base);
    hnd->phys = intptr_t(mFramebuffer->phys) + hnd->offset;
    *pHandle = hnd;

    return 0;
}

int BufferManager::fb_device_open(hw_module_t const* module, const char* name,
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
        dev->device.common.close = Display::closeDevice;
        dev->device.setSwapInterval = Display::setSwapInterval;
        dev->device.post            = Display::postBuffer;
        dev->device.setUpdateRect = 0;
        dev->device.compositionComplete = Display::compositionComplete;

        dev->device.common.module = const_cast<hw_module_t*>(module);

        BufferManager* pBufferManager = BufferManager::getInstance();
        if (pBufferManager == NULL) {
            ALOGE("%s get buffer manager failed.", __FUNCTION__);
            ::free(dev);
            return -EINVAL;
        }

        int fbid = atoi(name+2);
        if (fbid < 0 || fbid > 5) {
            ALOGE("%s invalid fb num %d", __FUNCTION__, fbid);
            ::free(dev);
            return -EINVAL;
        }

        int dispid = 0;
        dev->isMainDisp = 1;
        if (fbid != 0) {
            dispid = (int)*device;
            if (dispid < 0 || dispid >= MAX_DISPLAY_DEVICE) {
                ALOGE("%s invalid dispid %d", __FUNCTION__, dispid);
                ::free(dev);
                return -EINVAL;
            }
            dev->isMainDisp = 0;
        }

        ALOGI("fb_device_open dispid:%d, fb:%d", dispid, fbid);
        Display* display = pBufferManager->getDisplay(dispid);
        if (display == NULL) {
            ALOGE("%s can't get valid display", __FUNCTION__);
            ::free(dev);
            return -EINVAL;
        }

        status = display->initialize(fbid);
        if (status >= 0) {
            display->setContext(dev);
        }
        dev->display = display;

        *device = &dev->device.common;
        fbdev = (framebuffer_device_t*)(*device);
        fbdev->reserved[0] = nr_framebuffers;
    }

    return status;
}
