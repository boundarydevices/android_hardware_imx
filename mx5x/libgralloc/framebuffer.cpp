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

#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <GLES/gl.h>
#include <pthread.h>
#include <semaphore.h>

#include <cutils/log.h>
#include <hardware/gralloc.h>
#include <hardware/hardware.h>

#include "gralloc_priv.h"
#include "gr.h"
#define  MAX_RECT_NUM   20

// numbers of buffers for page flipping
#define NUM_BUFFERS 3

enum {
    PAGE_FLIP = 0x00000001,
    LOCKED = 0x00000002
};

struct fb_context_t {
    framebuffer_device_t  device;
};

static int nr_framebuffers;
static int no_ipu = 0;

sem_t * fslwatermark_sem_open()
{
    int fd;
    int ret;
    sem_t *pSem = NULL;
    char *shm_path, shm_file[256];

    shm_path = getenv("CODEC_SHM_PATH");      /*the CODEC_SHM_PATH is on a memory map the fs */ 

    if (shm_path == NULL)
        strcpy(shm_file, "/dev/shm");   /* default path */
    else
        strcpy(shm_file, shm_path);

    strcat(shm_file, "/"); 
    strcat(shm_file, "codec.shm");

    fd = open(shm_file, O_RDWR, 0666);
    if (fd < 0) { 
        /* first thread/process need codec protection come here */
        fd = open(shm_file, O_RDWR | O_CREAT | O_EXCL, 0666);
       if(fd < 0)
       {
           return NULL;
       }
       /* Change permissions of shmem, so that other process can access it */
       chmod(shm_file, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
       ftruncate(fd, sizeof(sem_t));

       /* map the semaphore variant in the file */ 
       pSem = (sem_t *)mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
       if((void *)(-1) == pSem)
       {
           return NULL;
       }
       /* do the semaphore initialization */
       ret = sem_init(pSem, 0, 1);
       if(-1 == ret)
       {
           return NULL;
       }
    }
    else
      pSem = (sem_t *)mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    close(fd);

    return pSem;
}

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
            ALOGE("FBIOPAN_DISPLAY failed");
            m->base.unlock(&m->base, buffer); 
            return -errno;
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
    glFinish();
    return 0;
}

int mapFrameBufferLocked(struct private_module_t* module)
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

    if(info.bits_per_pixel == 32){
        ALOGW("32bpp setting of Framebuffer catched!");
        /*
         * Explicitly request RGBA 8/8/8/8
         */
        info.bits_per_pixel = 32;
        info.red.offset     = 0;
        info.red.length     = 8;
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

    ALOGI(   "using (fd=%d)\n"
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

    ALOGI(   "width        = %d mm (%f dpi)\n"
            "height       = %d mm (%f dpi)\n"
            "refresh rate = %.2f Hz\n",
            info.width,  xdpi,
            info.height, ydpi,
            fps
    );


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
    module->framebuffer = new private_handle_t(dup(fd), fbSize,
            private_handle_t::PRIV_FLAGS_USES_ION);

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

static int mapFrameBuffer(struct private_module_t* module)
{
    pthread_mutex_lock(&module->lock);
    int err = mapFrameBufferLocked(module);
    pthread_mutex_unlock(&module->lock);
    return err;
}

static int fb_close(struct hw_device_t *dev)
{
    fb_context_t* ctx = (fb_context_t*)dev;
    if (ctx) {
        free(ctx);
    }
    return 0;
}

int fb_device_open(hw_module_t const* module, const char* name,
        hw_device_t** device)
{
    int status = -EINVAL;

    if (!strcmp(name, GRALLOC_HARDWARE_FB0)) {
        alloc_device_t* gralloc_device;
        framebuffer_device_t *fbdev;

        status = gralloc_open(module, &gralloc_device);
        if (status < 0)
            return status;

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
        dev->device.setUpdateRect = fb_setUpdateRect;
        dev->device.compositionComplete = fb_compositionComplete;

        private_module_t* m = (private_module_t*)module;
        status = mapFrameBuffer(m);
        if (status >= 0) {
            int stride = m->finfo.line_length / (m->info.bits_per_pixel >> 3);
            const_cast<uint32_t&>(dev->device.flags) = 0xfb0;
            const_cast<uint32_t&>(dev->device.width) = m->info.xres;
            const_cast<uint32_t&>(dev->device.height) = m->info.yres;
            const_cast<int&>(dev->device.stride) = stride;
            if(m->info.bits_per_pixel != 32) {
                const_cast<int&>(dev->device.format) = HAL_PIXEL_FORMAT_RGB_565;
            }
            else{
                const_cast<int&>(dev->device.format) = HAL_PIXEL_FORMAT_RGBA_8888;
            }
            const_cast<float&>(dev->device.xdpi) = m->xdpi;
            const_cast<float&>(dev->device.ydpi) = m->ydpi;
            const_cast<float&>(dev->device.fps) = m->fps;
            const_cast<int&>(dev->device.minSwapInterval) = 1;
            const_cast<int&>(dev->device.maxSwapInterval) = 1;
            const_cast<int&>(dev->device.numFramebuffers) = NUM_BUFFERS;
            *device = &dev->device.common;
            fbdev = (framebuffer_device_t*) *device;
      }

    fslwatermark_sem_open();

    }
    return status;
}
