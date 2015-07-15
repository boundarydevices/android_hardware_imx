/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc. All Rights Reserved.
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
#include <utils/StrongPointer.h>

#include <linux/mxcfb.h>
#include <linux/ioctl.h>
#include "hwc_context.h"

void hwc_get_display_type(struct hwc_context_t* ctx)
{
    if (ctx == NULL) {
        ALOGI("%s invalid cts", __FUNCTION__);
        return;
    }

    FILE *fp = NULL;
    const char* path = NULL;
    char value[HWC_STRING_LENGTH];

    path = HWC_PRIMARY_PATH"name";
    fp = fopen(path, "r");
    if (fp != NULL) {
        memset(value, 0, sizeof(value));
        if (fgets(value, sizeof(value), fp) != NULL) {
            if (strstr(value, "epdc")) {
                ALOGI("display is %s", value);
                ctx->mDisplayType = HWC_DISPLAY_EPDC;
                fclose(fp);
                return;
            }
        }
        fclose(fp);
    }

    path = HWC_PRIMARY_PATH"fsl_disp_dev_property";
    fp = fopen(path, "r");
    if (fp == NULL) {
        ALOGI("open %s failed", path);
        ctx->mDisplayType = HWC_DISPLAY_LCD;
        return;
    }
    memset(value, 0, sizeof(value));
    if (!fgets(value, sizeof(value), fp)) {
        ALOGI("read %s failed", path);
        ctx->mDisplayType = HWC_DISPLAY_LCD;
        fclose(fp);
        return;
    }

    if (strstr(value, "hdmi")) {
        ALOGI("display is %s", value);
        ctx->mDisplayType = HWC_DISPLAY_HDMI;
    }
    else if (strstr(value, "dvi")) {
        ALOGI("display is %s", value);
        ctx->mDisplayType = HWC_DISPLAY_DVI;
    }
    else {
        ALOGI("display is %s", value);
        ctx->mDisplayType = HWC_DISPLAY_LCD;
    }
    fclose(fp);
}

void hwc_get_framebuffer_info(struct hwc_context_t* ctx)
{
    int refreshRate = 0;
    struct fb_var_screeninfo info;

    if (ctx == NULL) {
        ALOGI("%s invalid cts", __FUNCTION__);
        return;
    }

    if (ctx->mFbFile < 0) {
        ALOGE("%s invalid fb handle assume 60 fps");
        ctx->mVsyncPeriod  = 1000000000 / 60;
        return;
    }

    if (ioctl(ctx->mFbFile, FBIOGET_VSCREENINFO, &info) < 0) {
        ALOGI("FBIOGET_VSCREENINFO ioctl failed: %s, assume 60 fps",
              strerror(errno));
        ctx->mVsyncPeriod  = 1000000000 / 60;
        return;
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

    ctx->mVsyncPeriod  = 1000000000 / refreshRate;
    ALOGI("<%s,%d> Vsync rate %d fps, frame time %llu ns", __FUNCTION__, __LINE__,
          refreshRate, ctx->mVsyncPeriod);
}

