/*
 * Copyright (C) 2009-2012 Freescale Semiconductor, Inc. All Rights Reserved.
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


#include <hardware/hardware.h>

#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/atomic.h>
#include <cutils/properties.h>
#include <utils/threads.h>
#include <hardware/hwcomposer.h>
#include <hardware_legacy/uevent.h>
#include <utils/StrongPointer.h>

#include <linux/mxcfb.h>
#include <linux/ioctl.h>
#include <EGL/egl.h>
#include "gralloc_priv.h"
#include "hwc_context.h"
#include "hwc_vsync.h"

static int hwc_judge_display_state(struct hwc_context_t* ctx)
{
    char fb_path[HWC_PATH_LENGTH];
    char tmp[HWC_PATH_LENGTH];
    char value[HWC_STRING_LENGTH];
    FILE *fp;
    int dispid = 0;

    for (int i=0; i<HWC_MAX_FB; i++) {
        if(dispid >= HWC_NUM_DISPLAY_TYPES) {
            ALOGW("system can't support more than %d devices", dispid);
            break;
        }

        displayInfo *pInfo = &ctx->mDispInfo[dispid];
        memset(fb_path, 0, sizeof(fb_path));
        snprintf(fb_path, HWC_PATH_LENGTH, HWC_FB_SYS"%d", i);
        //check the fb device exist.
        if (!(fp = fopen(fb_path, "r"))) {
            ALOGW("open %s failed", fb_path);
            continue;
        }
        fclose(fp);

        //check if it is a real device
        memset(tmp, 0, sizeof(tmp));
        strcpy(tmp, fb_path);
        strcat(tmp, "/name");
        if (!(fp = fopen(tmp, "r"))) {
            ALOGW("open %s failed", tmp);
            continue;
        }
        memset(value, 0, sizeof(value));
        if (!fgets(value, sizeof(value), fp)) {
            ALOGE("Unable to read fb%d name %s", i, tmp);
            fclose(fp);
            continue;
        }
        if (strstr(value, "FG")) {
            ALOGI("fb%d is overlay device", i);
            fclose(fp);
            continue;
        }
        fclose(fp);

        //read fb device name
        memset(tmp, 0, sizeof(tmp));
        strcpy(tmp, fb_path);
        strcat(tmp, "/fsl_disp_dev_property");
        if (!(fp = fopen(tmp, "r"))) {
            ALOGI("open %s failed", tmp);
            continue;
            //make default type to ldb.
            //pInfo->type = HWC_DISPLAY_LDB;
            //pInfo->connected = true;
        }
        else {
            memset(value, 0, sizeof(value));
            if (!fgets(value, sizeof(value), fp)) {
                ALOGI("read %s failed", tmp);
                continue;
                //make default type to ldb.
                //pInfo->type = HWC_DISPLAY_LDB;
                //pInfo->connected = true;
            }
            else if (strstr(value, "hdmi")) {
                ALOGI("fb%d is %s device", i, value);
                pInfo->type = HWC_DISPLAY_HDMI;
            }
            else if (strstr(value, "dvi")) {
                ALOGI("fb%d is %s device", i, value);
                pInfo->type = HWC_DISPLAY_DVI;
            }
            else {
                ALOGI("fb%d is %s device", i, value);
                pInfo->type = HWC_DISPLAY_LDB;
                pInfo->connected = true;
            }
            fclose(fp);
        }

        pInfo->fb_num = i;
        if(pInfo->type != HWC_DISPLAY_LDB) {
            //judge connected device state
            memset(tmp, 0, sizeof(tmp));
            strcpy(tmp, fb_path);
            strcat(tmp, "/disp_dev/cable_state");
            if (!(fp = fopen(tmp, "r"))) {
                ALOGI("open %s failed", tmp);
                //make default to false.
                pInfo->connected = false;
            }
            else {
                memset(value,  0, sizeof(value));
                if (!fgets(value, sizeof(value), fp)) {
                    ALOGI("read %s failed", tmp);
                    //make default to false.
                    pInfo->connected = false;
                }
                else if (strstr(value, "plugin")) {
                    ALOGI("fb%d device %s", i, value);
                    pInfo->connected = true;
                }
                else {
                    ALOGI("fb%d device %s", i, value);
                    pInfo->connected = false;
                }
                fclose(fp);
            }
        }

        dispid ++;
    }

    return 0;
}

int hwc_get_framebuffer_info(displayInfo *pInfo)
{
    char fb_path[HWC_PATH_LENGTH];
    int refreshRate;

    memset(fb_path, 0, sizeof(fb_path));
    snprintf(fb_path, HWC_PATH_LENGTH, HWC_FB_PATH"%d", pInfo->fb_num);
    pInfo->fd = open(fb_path, O_RDWR);
    if(pInfo->fd < 0) {
        ALOGE("open %s failed", fb_path);
        return BAD_VALUE;
    }

    struct fb_var_screeninfo info;
    if (ioctl(pInfo->fd, FBIOGET_VSCREENINFO, &info) == -1) {
        ALOGE("FBIOGET_VSCREENINFO ioctl failed: %s", strerror(errno));
        close(pInfo->fd);
        return BAD_VALUE;
    }

    refreshRate = 1000000000000LLU /
        (
         uint64_t( info.upper_margin + info.lower_margin + info.yres + info.vsync_len)
         * ( info.left_margin  + info.right_margin + info.xres + info.hsync_len)
         * info.pixclock
        );

    if (refreshRate == 0) {
        ALOGW("invalid refresh rate, assuming 60 Hz");
        refreshRate = 60;
    }

    pInfo->xres = info.xres;
    pInfo->yres = info.yres;
    pInfo->xdpi = 1000 * (info.xres * 25.4f) / info.width;
    pInfo->ydpi = 1000 * (info.yres * 25.4f) / info.height;
    pInfo->vsync_period  = 1000000000 / refreshRate;

    ALOGV("using\n"
          "xres         = %d px\n"
          "yres         = %d px\n"
          "width        = %d mm (%f dpi)\n"
          "height       = %d mm (%f dpi)\n"
          "refresh rate = %d Hz\n",
          dev->xres, dev->yres, info.width, dev->xdpi / 1000.0,
          info.height, dev->ydpi / 1000.0, refreshRate);

    return NO_ERROR;
}
#if 0
static int hwc_get_framebuffer_info(struct hwc_context_t* ctx)
{
    struct fb_var_screeninfo info;
    if (ioctl(ctx->m_mainfb_fd, FBIOGET_VSCREENINFO, &info) == -1) {
        ALOGE("<%s,%d> FBIOGET_VSCREENINFO failed", __FUNCTION__, __LINE__);
        return -errno;
    }

    int refreshRate = 1000000000000000LLU / (uint64_t(info.upper_margin +
                                                      info.lower_margin +
                                                      info.yres +
                                                      info.vsync_len) *
                                             (info.left_margin  +
                                              info.right_margin +
                                              info.xres +
                                              info.hsync_len) * info.pixclock);
    if (refreshRate == 0)
        refreshRate = 60 * 1000;  // 60 Hz

    ctx->m_mainfb_fps = refreshRate / 1000.0f;
    return 0;
}
#endif
int hwc_get_display_info(struct hwc_context_t* ctx)
{
    int err = 0;
    int dispid = 0;

    hwc_judge_display_state(ctx);
    for(dispid=0; dispid<HWC_NUM_DISPLAY_TYPES; dispid++) {
        displayInfo *pInfo = &ctx->mDispInfo[dispid];
        if(pInfo->connected) {
            err = hwc_get_framebuffer_info(pInfo);
        }
    }

    return err;
}


int hwc_get_display_fbid(struct hwc_context_t* ctx, int disp_type)
{
    int fbid = -1;
    int dispid = 0;
    for(dispid=0; dispid<HWC_NUM_DISPLAY_TYPES; dispid++) {
        displayInfo *pInfo = &ctx->mDispInfo[dispid];
        if(pInfo->type == disp_type) {
            fbid = pInfo->fb_num;
            break;
        }
    }

    return fbid;
}
