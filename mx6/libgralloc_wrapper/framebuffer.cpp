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

/* Copyright 2010-2012 Freescale Semiconductor Inc. */

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

#include <hardware/DisplayCommand.h>
#include "gralloc_priv.h"
#include <utils/String8.h>
/*****************************************************************************/

// numbers of buffers for page flipping
#define NUM_BUFFERS 3

enum {
    PAGE_FLIP = 0x00000001,
    LOCKED = 0x00000002
};

struct fb_context_t {
    framebuffer_device_t  device;
    int mainDisp_fd;
    private_module_t* priv_m;
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
            LOGE("<%s, %d> ioctl FBIOPAN_DISPLAY failed", __FUNCTION__, __LINE__);
            m->base.unlock(&m->base, buffer); 
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
static int set_graphics_fb_mode(int fb, struct configParam* param)
{
    char temp_name[256];
    char fb_mode[256];
    const char *disp_mode = NULL;
    int fd_mode = 0;
    int size=0;
    int n = 0;

    if(fb == 0 || param == NULL) {
        LOGE("<%s,%d> invalide parameter", __FUNCTION__, __LINE__);
        return 0;
    }

    String8 str8_mode(param->mode);
    disp_mode = str8_mode.string();
    if(disp_mode == NULL) {
        LOGE("<%s,%d> invalide parameter", __FUNCTION__, __LINE__);
        return -1;
    }
    memset(temp_name, 0, sizeof(temp_name));
    sprintf(temp_name, "/sys/class/graphics/fb%d/mode", fb);
    fd_mode = open(temp_name,O_RDWR, 0);
    if(fd_mode < 0) {
        LOGI("Error %d! Cannot open %s", fd_mode, temp_name);
        return -1;
    }

    memset(fb_mode, 0, sizeof(fb_mode));
    n = strlen(disp_mode);
    memcpy(fb_mode, disp_mode, n);
    fb_mode[n] = '\n';
    fb_mode[n + 1] = '\0';
    size = write(fd_mode, fb_mode, n + 2);
    if(size <= 0)
    {
        LOGI("Error %d %s! Cannot write %s", errno, strerror(errno), temp_name);
    }

    close(fd_mode);

    return 0;
}

static int mapFrameBufferWithParamLocked(struct private_module_t* module, struct configParam* param)
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
    int dpy = 0;

    if(param != NULL) {
        dpy = param->displayId;
    }
    set_graphics_fb_mode(dpy, param);

    while ((fd==-1) && device_template[i]) {
        snprintf(name, 64, device_template[i], dpy);
        fd = open(name, O_RDWR, 0);
        i++;
    }
    if (fd < 0) {
        LOGE("<%s,%d> open %s failed", __FUNCTION__, __LINE__, name);
        return -errno;
    }

    if(param != NULL) {
        int blank = FB_BLANK_UNBLANK;
        if(ioctl(fd, FBIOBLANK, blank) < 0) {
            LOGE("<%s, %d> ioctl FBIOBLANK failed", __FUNCTION__, __LINE__);
        }
    }

    struct fb_fix_screeninfo finfo;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        LOGE("<%s,%d> FBIOGET_FSCREENINFO failed", __FUNCTION__, __LINE__);
        return -errno;
    }

    struct fb_var_screeninfo info;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &info) == -1) {
        LOGE("<%s,%d> FBIOGET_VSCREENINFO failed", __FUNCTION__, __LINE__);
        return -errno;
    }

    if(param != NULL) {
        if(param->colorDepth == 16 || param->colorDepth == 32) {
            info.bits_per_pixel = param->colorDepth;
        }
        if(param->operateCode & OPERATE_CODE_CHANGE_RESOLUTION) {
            //info.xres = param->width;
            //info.yres = param->height;
        }

        if(info.bits_per_pixel == 0) { //|| info.xres == 0 || info.yres == 0 
            LOGE("<%s,%d> the bpp or xres yres is 0", __FUNCTION__, __LINE__);
            return -1;
        }
    }

    info.reserved[0] = 0;
    info.reserved[1] = 0;
    info.reserved[2] = 0;
    info.xoffset = 0;
    info.yoffset = 0;
    info.activate = FB_ACTIVATE_NOW;

    if(info.bits_per_pixel == 32){
        LOGW("32bpp setting of Framebuffer catched!");
        /*
         * Explicitly request BGRA 8/8/8
         */
        info.bits_per_pixel = 32;
        info.red.offset     = 8;
        info.red.length     = 8;
        info.green.offset   = 16;
        info.green.length   = 8;
        info.blue.offset    = 24;
        info.blue.length    = 8;
        info.transp.offset  = 0;
        info.transp.length  = 0;
        /*
         *  set the alpha in pixel
         *  only when the fb set to 32bit
         */
        struct mxcfb_loc_alpha l_alpha;
        l_alpha.enable = true;
        l_alpha.alpha_in_pixel = true;
        if (ioctl(fd, MXCFB_SET_LOC_ALPHA,
                    &l_alpha) < 0) {
            LOGE("<%s,%d> set local alpha failed", __FUNCTION__, __LINE__);
            close(fd);
            return -errno;
        }
    }
    else{
        /*
         * Explicitly request 5/6/5
         */
        info.bits_per_pixel = 16;
        info.red.offset     = 11;
        info.red.length     = 5;
        info.green.offset   = 5;
        info.green.length   = 6;
        info.blue.offset    = 0;
        info.blue.length    = 5;
        info.transp.offset  = 0;
        info.transp.length  = 0;

        if (!no_ipu) {
            /* for the 16bit case, only involke the glb alpha */
            struct mxcfb_gbl_alpha gbl_alpha;

            gbl_alpha.alpha = 255;
            gbl_alpha.enable = 1;
            int ret = ioctl(fd, MXCFB_SET_GBL_ALPHA, &gbl_alpha);
            if(ret <0) {
	        LOGE("<%s,%d> Error!MXCFB_SET_GBL_ALPHA failed!", __FUNCTION__, __LINE__);
	        return -1;
            }

            struct mxcfb_color_key key;
            key.enable = 1;
            key.color_key = 0x00000000; // Black
            ret = ioctl(fd, MXCFB_SET_CLR_KEY, &key);
            if(ret <0) {
	        LOGE("<%s,%d> Error!Colorkey setting failed for dev ", __FUNCTION__, __LINE__);
	        return -1;
            }
        }
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
        LOGW("FBIOPUT_VSCREENINFO failed, page flipping not supported");
    }

    if (info.yres_virtual < ALIGN_PIXEL_128(info.yres) * 2) {
        // we need at least 2 for page-flipping
        info.yres_virtual = ALIGN_PIXEL_128(info.yres);
        flags &= ~PAGE_FLIP;
        LOGW("page flipping not supported (yres_virtual=%d, requested=%d)",
                info.yres_virtual, ALIGN_PIXEL_128(info.yres)*2);
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &info) == -1) {
        LOGE("<%s,%d> FBIOGET_VSCREENINFO failed", __FUNCTION__, __LINE__);
        return -errno;
    }

    int refreshRate = 1000000000000000LLU /
    (
            uint64_t( info.upper_margin + info.lower_margin + info.yres )
            * ( info.left_margin  + info.right_margin + info.xres )
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

    LOGW(   "using (fd=%d)\n"
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

    LOGW(   "width        = %d mm (%f dpi)\n"
            "height       = %d mm (%f dpi)\n"
            "refresh rate = %.2f Hz\n",
            info.width,  xdpi,
            info.height, ydpi,
            fps
    );


    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        LOGE("<%s,%d> FBIOGET_FSCREENINFO failed", __FUNCTION__, __LINE__);
        return -errno;
    }

    if (finfo.smem_len <= 0) {
        LOGE("<%s,%d> finfo.smem_len <= 0", __FUNCTION__, __LINE__);
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
    module->framebuffer = new private_handle_t(dup(fd), fbSize,
            private_handle_t::PRIV_FLAGS_USES_PMEM);

    module->numBuffers = info.yres_virtual / ALIGN_PIXEL_128(info.yres);
    module->bufferMask = 0;

    void* vaddr = mmap(0, fbSize, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (vaddr == MAP_FAILED) {
        LOGE("Error mapping the framebuffer (%s)", strerror(errno));
        return -errno;
    }
    module->framebuffer->base = intptr_t(vaddr);
    module->framebuffer->phys = intptr_t(finfo.smem_start);
    memset(vaddr, 0, fbSize);
    return 0;
}

int mapFrameBufferLocked(struct private_module_t* module)
{
    return mapFrameBufferWithParamLocked(module, NULL);
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
    int err = 0;
    pthread_mutex_lock(&module->lock);

    size_t fbSize = module->framebuffer->size;
    int fd = module->framebuffer->fd;
    void* addr = (void*)(module->framebuffer->base);
    munmap(addr, fbSize);
    delete (module->framebuffer);
    module->framebuffer = NULL;

    int blank = 1;
    if(ioctl(fd, FBIOBLANK, blank) < 0) {
        LOGE("<%s, %d> ioctl FBIOBLANK failed", __FUNCTION__, __LINE__);
    }

    if(ctx->mainDisp_fd > 0) {
        blank = FB_BLANK_UNBLANK;
        if(ioctl(ctx->mainDisp_fd, FBIOBLANK, blank) < 0) {
            LOGE("<%s, %d> ioctl FBIOBLANK failed", __FUNCTION__, __LINE__);
        }
    }
    close(fd);
    pthread_mutex_unlock(&module->lock);

    return err;
}

static int mapFrameBufferWithParam(struct private_module_t* module, struct configParam* param)
{
    pthread_mutex_lock(&module->lock);
    int err = mapFrameBufferWithParamLocked(module, param);
    pthread_mutex_unlock(&module->lock);
    return err;
}
/*****************************************************************************/

static int fb_close(struct hw_device_t *dev)
{
    fb_context_t* ctx = (fb_context_t*)dev;
    if (ctx) {
        if (ctx->priv_m != NULL) {
            unMapFrameBuffer(ctx, ctx->priv_m);
            free(ctx->priv_m);
        }
        free(ctx);
    }
    return 0;
}

static void fb_device_init(private_module_t* m, fb_context_t *dev);

static int fb_perform(struct gralloc_module_t const* module,
        int operation, ... )
{
    int err = 0;
    va_list args;

    va_start(args, operation);
    struct configParam* param= va_arg(args, struct configParam*);
    fb_context_t* ctx = va_arg(args, fb_context_t*);
    private_module_t* pm = (private_module_t*)module;
    switch(operation & 0xf000) {
        case OPERATE_CODE_ENABLE:
            err = mapFrameBufferWithParam(pm, param);
            if(err >= 0) {
                fb_device_init(pm, ctx);
            }
            break;

        case OPERATE_CODE_DISABLE:
            err = unMapFrameBuffer(ctx, pm);
            break;

        case OPERATE_CODE_CHANGE:
            switch(operation & 0x0fff) {
                case OPERATE_CODE_CHANGE_RESOLUTION:
                case OPERATE_CODE_CHANGE_COLORDEPTH:
		    err = unMapFrameBuffer(ctx, pm);
		    err = mapFrameBufferWithParam(pm, param);
		    if(err >= 0) {
			fb_device_init(pm, ctx);
		    }
                    break;
                default:
                    LOGE("<%s, %d> invalide operate code %d!", __FUNCTION__, __LINE__, (int)operation);
                    err = -1;
                    break;
            } 
            break;
        default:
            LOGE("<%s, %d> invalide operate code %d!", __FUNCTION__, __LINE__, (int)operation);
            err = -1;
            break;
    }

    va_end(args);
 
    return err;
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
	const_cast<int&>(dev->device.format) = HAL_PIXEL_FORMAT_BGRA_8888;
    }
    const_cast<float&>(dev->device.xdpi) = m->xdpi;
    const_cast<float&>(dev->device.ydpi) = m->ydpi;
    const_cast<float&>(dev->device.fps) = m->fps;
    const_cast<int&>(dev->device.minSwapInterval) = 1;
    const_cast<int&>(dev->device.maxSwapInterval) = 1;

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
            no_ipu = 1;
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

            dev->priv_m = NULL;
            dev->mainDisp_fd = 0;
            fslwatermark_sem_open();
        } else {
            private_module_t* orig_m = (private_module_t*)module;
            private_module_t* priv_m = (private_module_t*)malloc(sizeof(*priv_m));
            memset(priv_m, 0, sizeof(*priv_m));
            memcpy(priv_m, orig_m, sizeof(*priv_m));

            dev->device.common.module = (hw_module_t*)(priv_m);
            priv_m->framebuffer = NULL;
            priv_m->gpu_device = 0;

            gralloc_module_t* gra_m = reinterpret_cast<gralloc_module_t*>(priv_m);
            gra_m->perform = fb_perform;
            dev->priv_m = priv_m;
            dev->mainDisp_fd = orig_m->framebuffer->fd;
        }

        *device = &dev->device.common;
        fbdev = (framebuffer_device_t*) *device;
        fbdev->reserved[0] = nr_framebuffers;
    } 

    return status;
}
