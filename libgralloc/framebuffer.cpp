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

#ifdef SECOND_DISPLAY_SUPPORT
extern "C" {
#include "mxc_ipu_hl_lib.h" 
} 
#endif

#endif
#include <GLES/gl.h>

#include "gralloc_priv.h"
#include "gr.h"

/*****************************************************************************/

// numbers of buffers for page flipping
#define NUM_BUFFERS 2


enum {
    PAGE_FLIP = 0x00000001,
    LOCKED = 0x00000002
};

struct fb_context_t {
    framebuffer_device_t  device;
    #ifdef FSL_EPDC_FB
    //Partial udate feature
    bool partial_update;
    int partial_left;
    int partial_top;
    int partial_width;
    int partial_height;
    #endif
    #ifdef SECOND_DISPLAY_SUPPORT
    bool sec_display_inited;
    int sec_fp;
    int sec_disp_w;
    int sec_disp_h;
    int sec_disp_base;
    int sec_disp_phys;
    int sec_frame_size;
    int sec_disp_next_buf;
    struct fb_var_screeninfo sec_info;
    struct fb_fix_screeninfo sec_finfo;
    #endif
};

#ifdef SECOND_DISPLAY_SUPPORT
#define MAX_SEC_DISP_WIDTH (1024)
#define MAX_SEC_DISP_HEIGHT (1024)
static int mapSecFrameBuffer(fb_context_t* ctx);
static int resizeToSecFrameBuffer(int base,int phys,fb_context_t* ctx);
#endif

#ifdef FSL_EPDC_FB
#define WAVEFORM_MODE_INIT                      0x0   // Screen goes to white (clears)
#define WAVEFORM_MODE_DU                        0x1   // Grey->white/grey->black
#define WAVEFORM_MODE_GC16                      0x2   // High fidelity (flashing)
#define WAVEFORM_MODE_GC4                       0x3  //

__u32 marker_val = 1;
static void update_to_display(int left, int top, int width, int height, int wave_mode, int wait_for_complete, int fb_dev)
{
	struct mxcfb_update_data upd_data;
	int retval;
	memset(&upd_data, 0, sizeof(mxcfb_update_data));
	//upd_data.update_mode = UPDATE_MODE_FULL;
    upd_data.update_mode = UPDATE_MODE_PARTIAL;
	upd_data.waveform_mode = wave_mode;
	upd_data.update_region.left = left;
	upd_data.update_region.width = width;
	upd_data.update_region.top = top;
	upd_data.update_region.height = height;

	if (wait_for_complete) {
		/* Get unique marker value */
		upd_data.update_marker = marker_val++;
	} else {
		upd_data.update_marker = 0;
	}

	retval = ioctl(fb_dev, MXCFB_SEND_UPDATE, &upd_data);
	while (retval < 0) {
		/* We have limited memory available for updates, so wait and
		 * then try again after some updates have completed */
		sleep(1);
		retval = ioctl(fb_dev, MXCFB_SEND_UPDATE, &upd_data);
        LOGI("MXCFB_SEND_UPDATE  retval = 0x%x try again maybe", retval);
	}

	if (wait_for_complete) {
		/* Wait for update to complete */
		retval = ioctl(fb_dev, MXCFB_WAIT_FOR_UPDATE_COMPLETE, &upd_data.update_marker);
		if (retval < 0) {
			LOGI("Wait for update complete failed.  Error = 0x%x", retval);
		}
	}
}
#endif

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
#ifdef FSL_EPDC_FB
    fb_context_t* ctx = (fb_context_t*)dev;
    ctx->partial_update = true;
    ctx->partial_left = l;
    ctx->partial_top = t;
    ctx->partial_width = w;
    ctx->partial_height = h;    
#endif
    return 0;
}

static int fb_post(struct framebuffer_device_t* dev, buffer_handle_t buffer)
{
    if (private_handle_t::validate(buffer) < 0)
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

        m->base.lock(&m->base, buffer, 
                private_module_t::PRIV_USAGE_LOCKED_FOR_POST, 
                0, 0, ALIGN_PIXEL(m->info.xres), ALIGN_PIXEL_128(m->info.yres), NULL);

        const size_t offset = hnd->base - m->framebuffer->base;
        m->info.activate = FB_ACTIVATE_VBL;
        m->info.yoffset = offset / m->finfo.line_length;

        #ifdef SECOND_DISPLAY_SUPPORT
        //Check the prop rw.SECOND_DISPLAY_CONNECTED
        char value[PROPERTY_VALUE_MAX];
     
        property_get("rw.SECOND_DISPLAY_CONNECTED", value, "");
        if (strcmp(value, "1") == 0) {
            if(!ctx->sec_display_inited) {
                //Init the second display
                if(mapSecFrameBuffer(ctx)== 0)
                {    
                    ctx->sec_display_inited = true;
                    //Set the prop rw.SECOND_DISPLAY_ENABLED to 1
                    LOGI("sys.SECOND_DISPLAY_ENABLED Set to 1");
                    property_set("sys.SECOND_DISPLAY_ENABLED", "1");
                }
            }

            if(ctx->sec_display_inited) {
                //Resize the primary display to the second display
                resizeToSecFrameBuffer(hnd->base,
                                       m->framebuffer->phys+offset,
                                       ctx);
                ctx->sec_info.activate = FB_ACTIVATE_VBL;
                if(!ctx->sec_disp_next_buf) {
                    ctx->sec_info.yoffset = 0; 
                }
                else{
                    ctx->sec_info.yoffset = ctx->sec_disp_h;
                }

                ctx->sec_disp_next_buf = !ctx->sec_disp_next_buf;
                ioctl(ctx->sec_fp, FBIOPAN_DISPLAY, &ctx->sec_info);
            }
        }
        else{
            if(ctx->sec_display_inited) {
                //Set the prop rw.SECOND_DISPLAY_ENABLED to 0
                LOGI("Switch back to display 0");
                LOGI("sys.SECOND_DISPLAY_ENABLED Set to 0");
                property_set("sys.SECOND_DISPLAY_ENABLED", "0");
                //DeInit the second display
                if(ctx->sec_fp) {
                    int fp_property = open("/sys/class/graphics/fb1/fsl_disp_property",O_RDWR, 0); 
                    if(fp_property >= 0) {
                        char overlayStr[32];
                        int blank;
                        int fb2_fp;

                        blank = 1;

                        fb2_fp = open("/dev/graphics/fb2",O_RDWR, 0);
                        if (fb2_fp < 0){
                            LOGE("Error!Cannot open the /dev/graphics/fb2");
                        }
                        else{
                            if(ioctl(fb2_fp, FBIOBLANK, blank) < 0) {
                        		LOGI("Error!BLANK FB2 failed!\n");
                        	}
                            close(fb2_fp);
                        }

                    	if(ioctl(ctx->sec_fp, FBIOBLANK, blank) < 0) {
                    		LOGI("Error!BLANK FB1 failed!\n");
                    	}
                    
                        if(ioctl(m->framebuffer->fd, FBIOBLANK, blank) < 0) {
                    		LOGI("Error!BLANK FB0 failed!\n");
                    	}

                        memset(overlayStr, 0 ,32);
                        strcpy(overlayStr, "1-layer-fb\n");
                        LOGI("WRITE 1-layer-fb to fb0/fsl_disp_property");
                        write(fp_property, overlayStr, strlen(overlayStr)+1);
                        close(fp_property);

                        blank = FB_BLANK_UNBLANK;
                    	if(ioctl(ctx->sec_fp, FBIOBLANK, blank) < 0) {
                    		LOGI("Error!BLANK FB1 failed!\n");
                    	}
                    	if(ioctl(m->framebuffer->fd, FBIOBLANK, blank) < 0) {
                    		LOGI("Error!UNBLANK FB0 failed!\n");
                    	}

                    }
                    
                    close(ctx->sec_fp);
                    ctx->sec_fp = 0;
                }
                ctx->sec_display_inited = false;
            }
        }

        #endif

        if (ioctl(m->framebuffer->fd, FBIOPAN_DISPLAY, &m->info) == -1) {
            LOGE("FBIOPAN_DISPLAY failed");
            m->base.unlock(&m->base, buffer); 
            return -errno;
        }

        #ifdef FSL_EPDC_FB
        if(ctx->partial_update) {
            update_to_display(ctx->partial_left,ctx->partial_top,
                              ctx->partial_width,ctx->partial_height,
                              WAVEFORM_MODE_GC16,1,m->framebuffer->fd);
            ctx->partial_update = false;
        }
        else{
            update_to_display(0,0,m->info.xres,m->info.yres,WAVEFORM_MODE_GC16,1,m->framebuffer->fd);
        }
        #endif

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

        #ifdef FSL_EPDC_FB
        if(ctx->partial_update) {
            update_to_display(ctx->partial_left,ctx->partial_top,
                              ctx->partial_width,ctx->partial_height,
                              WAVEFORM_MODE_GC16,1,m->framebuffer->fd);
            ctx->partial_update = false;
        }
        else{
            update_to_display(0,0,m->info.xres,m->info.yres,WAVEFORM_MODE_GC16,1,m->framebuffer->fd);
        }
        #endif

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

/*****************************************************************************/

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

    char value[PROPERTY_VALUE_MAX];
    property_get("ro.UI_TVOUT_DISPLAY", value, "");
    if (strcmp(value, "1") != 0) {
        while ((fd==-1) && device_template[i]) {
            snprintf(name, 64, device_template[i], 0);
            fd = open(name, O_RDWR, 0);
            i++;
        }
    }
    else{
        while ((fd==-1) && device_template[i]) {
            snprintf(name, 64, device_template[i], 1);
            fd = open(name, O_RDWR, 0);
            i++;
        }
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

    /*
     * Request NUM_BUFFERS screens (at lest 2 for page flipping)
     */
    info.yres_virtual = ALIGN_PIXEL_128(info.yres) * NUM_BUFFERS;
	info.xres_virtual = ALIGN_PIXEL(info.xres);
    
    #ifdef FSL_EPDC_FB
    info.yres_virtual = ALIGN_PIXEL_128(info.yres);
	info.bits_per_pixel = 16;
	info.grayscale = 0;
	info.yoffset = 0;
	info.rotate = FB_ROTATE_UR;
    #endif

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

    if (ioctl(fd, FBIOGET_VSCREENINFO, &info) == -1)
        return -errno;

    #ifdef FSL_EPDC_FB
	int auto_update_mode = AUTO_UPDATE_MODE_REGION_MODE;
	int retval = ioctl(fd, MXCFB_SET_AUTO_UPDATE_MODE, &auto_update_mode);
	if (retval < 0)
	{
		return -errno;
	}
    #endif
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

    LOGI(   "using (fd=%d)\n"
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

    LOGI(   "width        = %d mm (%f dpi)\n"
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

static int mapFrameBuffer(struct private_module_t* module)
{
    pthread_mutex_lock(&module->lock);
    int err = mapFrameBufferLocked(module);
    pthread_mutex_unlock(&module->lock);
    return err;
}

#ifdef SECOND_DISPLAY_SUPPORT
static int mapSecFrameBuffer(fb_context_t* ctx)
{
    int retCode = 0;
    int sec_fp = 0,fp_property = 0;
    size_t fbSize = 0;
    int blank;
    void* vaddr = NULL;
    //struct mxcfb_gbl_alpha gbl_alpha;
    struct mxcfb_color_key key; 
    char overlayStr[32];
    int fb2_fp;
    private_module_t* m = reinterpret_cast<private_module_t*>(
            ctx->device.common.module);

    sec_fp = open("/dev/graphics/fb1",O_RDWR, 0);
    if (sec_fp < 0){
        LOGE("Error!Cannot open the /dev/graphics/fb1 for second display");
        goto disp_init_error;
    }

    //Switch overlay to second display after ipu deinit
    //echo 1-layer-fb >  /sys/class/graphics/fb0/fsl_disp_property 
    blank = 1;

    fb2_fp = open("/dev/graphics/fb2",O_RDWR, 0);
    if (fb2_fp < 0){
        LOGE("Error!Cannot open the /dev/graphics/fb2");
        goto disp_init_error;
    }
    if(ioctl(fb2_fp, FBIOBLANK, blank) < 0) {
		LOGI("Error!BLANK FB0 failed!\n");
        goto disp_init_error;
	}
    close(fb2_fp);

	if(ioctl(sec_fp, FBIOBLANK, blank) < 0) {
		LOGI("Error!BLANK FB1 failed!\n");
        goto disp_init_error;
	}

    if(ioctl(m->framebuffer->fd, FBIOBLANK, blank) < 0) {
		LOGI("Error!BLANK FB0 failed!\n");
        goto disp_init_error;
	}
    
    LOGI("Open fb0/fsl_disp_property");
    fp_property = open("/sys/class/graphics/fb0/fsl_disp_property",O_RDWR, 0); 
    if(fp_property < 0) {
         LOGI("Error!Cannot switch the overlay to second disp");
         goto disp_init_error;
    }
    
    memset(overlayStr, 0 ,32);
    strcpy(overlayStr, "1-layer-fb\n");
    LOGI("WRITE 1-layer-fb to fb0/fsl_disp_property");
    write(fp_property, overlayStr, strlen(overlayStr)+1);
    close(fp_property);

    blank = FB_BLANK_UNBLANK;
	if(ioctl(sec_fp, FBIOBLANK, blank) < 0) {
		LOGI("Error!UNBLANK FB1 failed!\n");
        goto disp_init_error;
	}

	if(ioctl(m->framebuffer->fd, FBIOBLANK, blank) < 0) {
		LOGI("Error!UNBLANK FB0 failed!\n");
        goto disp_init_error;
	}

    struct fb_fix_screeninfo finfo;
    if (ioctl(sec_fp, FBIOGET_FSCREENINFO, &finfo) == -1)
       goto disp_init_error;
                
    struct fb_var_screeninfo info;
    if (ioctl(sec_fp, FBIOGET_VSCREENINFO, &info) == -1)
        goto disp_init_error;
                
    LOGI("Second display: xres %d,yres %d, xres_virtual %d, yres_virtual %d",
         info.xres,info.xres_virtual,info.yres,info.yres_virtual);

    info.reserved[0] = 0;
    info.reserved[1] = 0;
    info.reserved[2] = 0;
    info.xoffset = 0;
    info.yoffset = 0;
    info.activate = FB_ACTIVATE_NOW;
                
    /*
    * Explicitly request 5/6/5
    */
    info.bits_per_pixel = 16;
    info.nonstd = 0;
    info.red.offset     = 11;
    info.red.length     = 5;
    info.green.offset   = 5;
    info.green.length   = 6;
    info.blue.offset    = 0;
    info.blue.length    = 5;
    info.transp.offset  = 0;
    info.transp.length  = 0;
    info.yres_virtual = info.yres * NUM_BUFFERS;
	info.xres_virtual = info.xres;
                        
    if (ioctl(sec_fp, FBIOPUT_VSCREENINFO, &info) == -1) {
        LOGE("Error!Second display FBIOPUT_VSCREENINFO");
        goto disp_init_error;
    }
                    
    if (ioctl(sec_fp, FBIOGET_VSCREENINFO, &info) == -1){
        LOGE("Error!Second display FBIOGET_VSCREENINFO");
        goto disp_init_error;
    }
                    
    if (ioctl(sec_fp, FBIOGET_FSCREENINFO, &finfo) == -1){
        LOGE("Error!Second display FBIOGET_FSCREENINFO");
        goto disp_init_error;
    }
                    
    if(finfo.smem_len <= 0)
        goto disp_init_error;

    fbSize = roundUpToPageSize(finfo.line_length * info.yres_virtual);  
                   
    vaddr = mmap(0, fbSize, PROT_READ|PROT_WRITE, MAP_SHARED, sec_fp, 0);
    if (vaddr == MAP_FAILED) {
        LOGE("Error!mapping the framebuffer (%s)", strerror(errno));
        goto disp_init_error;
    }

    key.enable = 1;
    key.color_key = 0x00000000; // Black
    LOGI("MXCFB_SET_CLR_KEY");
    if( ioctl(sec_fp, MXCFB_SET_CLR_KEY, &key) < 0)
    {
        LOGE("Error!MXCFB_SET_CLR_KEY");
        goto disp_init_error;
    }
                            
    //gbl_alpha.alpha = 255;
    //gbl_alpha.enable = 1;
    //LOGI("MXCFB_SET_GBL_ALPHA");
    //if(ioctl(sec_fp, MXCFB_SET_GBL_ALPHA, &key) <0)
    //{
    //    LOGI("Error!MXCFB_SET_GBL_ALPHA error");
    //    goto disp_init_error;
    //}

    ctx->sec_disp_base = intptr_t(vaddr);
    ctx->sec_disp_phys = intptr_t(finfo.smem_start);
    memset(vaddr, 0, fbSize);
    ctx->sec_fp = sec_fp;
    ctx->sec_disp_w = info.xres;
    ctx->sec_disp_h = info.yres;
    ctx->sec_frame_size = fbSize/NUM_BUFFERS;
    ctx->sec_disp_next_buf = 0;
    ctx->sec_info = info;
    ctx->sec_finfo = finfo;

    struct fb_fix_screeninfo fb0_finfo;
    if (ioctl(m->framebuffer->fd, FBIOGET_FSCREENINFO, &fb0_finfo) == -1)
       goto disp_init_error;
                
    struct fb_var_screeninfo fb0_info;
    if (ioctl(m->framebuffer->fd, FBIOGET_VSCREENINFO, &fb0_info) == -1)
        goto disp_init_error;
                
    LOGI("fb0_info display: xres %d,yres %d, xres_virtual %d, yres_virtual %d",
         fb0_info.xres,fb0_info.xres_virtual,
         fb0_info.yres,fb0_info.yres_virtual);

    fb0_info.reserved[0] = 0;
    fb0_info.reserved[1] = 0;
    fb0_info.reserved[2] = 0;
    fb0_info.xoffset = 0;
    fb0_info.yoffset = 0;
    fb0_info.activate = FB_ACTIVATE_NOW;
                
    /*
    * Explicitly request 5/6/5
    */
    fb0_info.bits_per_pixel = 16;
    fb0_info.nonstd = 0;
    fb0_info.red.offset     = 11;
    fb0_info.red.length     = 5;
    fb0_info.green.offset   = 5;
    fb0_info.green.length   = 6;
    fb0_info.blue.offset    = 0;
    fb0_info.blue.length    = 5;
    fb0_info.transp.offset  = 0;
    fb0_info.transp.length  = 0;
    fb0_info.yres_virtual = ALIGN_PIXEL_128(fb0_info.yres) * NUM_BUFFERS;
	fb0_info.xres_virtual = fb0_info.xres;
                        
    if (ioctl(m->framebuffer->fd, FBIOPUT_VSCREENINFO, &fb0_info) == -1) {
        LOGE("Error!Second display FBIOPUT_VSCREENINFO");
        goto disp_init_error;
    }

    return 0;

 disp_init_error:
    if(sec_fp) {
        close(sec_fp);
        sec_fp = 0;
        ctx->sec_fp = 0;
    }
    return -1;
}

static int resizeToSecFrameBuffer(int base,int phys,fb_context_t* ctx)
{
    ipu_lib_input_param_t sIPUInputParam;   
    ipu_lib_output_param_t sIPUOutputParam; 
    ipu_lib_handle_t            sIPUHandle;
    int iIPURet = 0;
    memset(&sIPUInputParam,0,sizeof(sIPUInputParam));
    memset(&sIPUOutputParam,0,sizeof(sIPUOutputParam));
    memset(&sIPUHandle,0,sizeof(sIPUHandle));

    //Setting input format
    sIPUInputParam.width = ctx->device.width;
    sIPUInputParam.height = ctx->device.height;

    sIPUInputParam.input_crop_win.pos.x = 0;
    sIPUInputParam.input_crop_win.pos.y = 0;  
    sIPUInputParam.input_crop_win.win_w = ctx->device.width;
    sIPUInputParam.input_crop_win.win_h = ctx->device.height;
    sIPUInputParam.fmt = v4l2_fourcc('R', 'G', 'B', 'P');
    sIPUInputParam.user_def_paddr[0] = phys;
        
    //Setting output format
    //Should align with v4l
    sIPUOutputParam.fmt = v4l2_fourcc('R', 'G', 'B', 'P');
    sIPUOutputParam.width = ctx->sec_disp_w;
    sIPUOutputParam.height = ctx->sec_disp_h;   
    sIPUOutputParam.show_to_fb = 0;
    //Output param should be same as input, since no resize,crop
    sIPUOutputParam.output_win.pos.x = 0;
    sIPUOutputParam.output_win.pos.y = 0;
    sIPUOutputParam.output_win.win_w = ctx->sec_disp_w;
    sIPUOutputParam.output_win.win_h = ctx->sec_disp_h;

    if((ctx->sec_disp_w > MAX_SEC_DISP_WIDTH)||
       (ctx->sec_disp_h > MAX_SEC_DISP_HEIGHT)) {
        sIPUOutputParam.output_win.win_w = MAX_SEC_DISP_WIDTH;
        if(ctx->sec_disp_h > MAX_SEC_DISP_HEIGHT) {
            sIPUOutputParam.output_win.win_h = ((ctx->sec_disp_h*MAX_SEC_DISP_WIDTH/ctx->sec_disp_w)>>3)<<3;
        }
        else{
            sIPUOutputParam.output_win.win_h = ctx->sec_disp_h;
        }
        if(sIPUOutputParam.output_win.win_w < ctx->sec_disp_w) {
            sIPUOutputParam.output_win.pos.x = (ctx->sec_disp_w - sIPUOutputParam.output_win.win_w)/2;
        }
    }

    sIPUOutputParam.rot = 0;
    sIPUOutputParam.user_def_paddr[0] = ctx->sec_disp_phys + ctx->sec_disp_next_buf*ctx->sec_frame_size;
    //LOGI("Output param: width %d,height %d, pos.x %d, pos.y %d,win_w %d,win_h %d,rot %d",
    //sIPUOutputParam.width,
    //sIPUOutputParam.height,
    //sIPUOutputParam.output_win.pos.x,
    //sIPUOutputParam.output_win.pos.y,
    //sIPUOutputParam.output_win.win_w,
    //sIPUOutputParam.output_win.win_h,
    //sIPUOutputParam.rot);
                                         
    //LOGI("Input param: width %d, height %d, fmt %d, crop_win pos x %d, crop_win pos y %d, crop_win win_w %d,crop_win win_h %d",
    //sIPUInputParam.width,
    //sIPUInputParam.height,
    //sIPUInputParam.fmt,
    //sIPUInputParam.input_crop_win.pos.x,
    //sIPUInputParam.input_crop_win.pos.y,
    //sIPUInputParam.input_crop_win.win_w,
    //sIPUInputParam.input_crop_win.win_h);     
        
    iIPURet =  mxc_ipu_lib_task_init(&sIPUInputParam,NULL,&sIPUOutputParam,NULL,OP_NORMAL_MODE|TASK_VF_MODE,&sIPUHandle);
    if (iIPURet < 0) {
        LOGE("Error!mxc_ipu_lib_task_init failed mIPURet %d!",iIPURet);
        return -1;
    }  
    //LOGI("mxc_ipu_lib_task_init success");
    iIPURet = mxc_ipu_lib_task_buf_update(&sIPUHandle,phys,sIPUOutputParam.user_def_paddr[0],NULL,NULL,NULL);
    if (iIPURet < 0) {
        LOGE("Error!mxc_ipu_lib_task_buf_update failed mIPURet %d!",iIPURet);
        mxc_ipu_lib_task_uninit(&sIPUHandle);
        return -1;
    }
    //LOGI("mxc_ipu_lib_task_buf_update success");
    mxc_ipu_lib_task_uninit(&sIPUHandle);

    return 0;
}
#endif

/*****************************************************************************/

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
        #ifndef FSL_EPDC_FB
        dev->device.setUpdateRect = 0;
        #else
        dev->device.setUpdateRect = fb_setUpdateRect;
        #endif
        dev->device.compositionComplete = fb_compositionComplete;

        private_module_t* m = (private_module_t*)module;
        status = mapFrameBuffer(m);
        if (status >= 0) {
            int stride = m->finfo.line_length / (m->info.bits_per_pixel >> 3);
            const_cast<uint32_t&>(dev->device.flags) = 0;
            const_cast<uint32_t&>(dev->device.width) = m->info.xres;
            const_cast<uint32_t&>(dev->device.height) = m->info.yres;
            const_cast<int&>(dev->device.stride) = stride;
            const_cast<int&>(dev->device.format) = HAL_PIXEL_FORMAT_RGB_565;
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
