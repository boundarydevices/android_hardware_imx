/*
 * Copyright (C) 2012-2014 Freescale Semiconductor, Inc. All Rights Reserved.
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
#ifndef FSL_HWC_CONTEXT_H_
#define FSL_HWC_CONTEXT_H_

#include <hardware/hardware.h>

#include <fcntl.h>
#include <errno.h>

#undef LOG_TAG
#define LOG_TAG "FslHwcomposer"
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
#include <hwc_context.h>
#include <g2d.h>

/*****************************************************************************/
#define HWC_FSL_HARDWARE_MODULE_ID "hwcomposer_fsl"
#define HWC_MAIN_FB "/dev/graphics/fb0"
#define HWC_MAX_FB 6
#define HWC_PATH_LENGTH 256
#define HWC_STRING_LENGTH 32
#define HWC_FB_PATH "/dev/graphics/fb"
#define HWC_FB_SYS "/sys/class/graphics/fb"

#ifndef NUM_FRAMEBUFFER_SURFACE_BUFFERS
#define NUM_FRAMEBUFFER_SURFACE_BUFFERS (2)
#endif

#define HWC_MAX_FRAMEBUFFER NUM_FRAMEBUFFER_SURFACE_BUFFERS

struct disp_private {
    int xres;
    int yres;
    bool connected;
    sp<ANativeWindow> mDisplaySurface;
    hwc_region_t mWormHole;
    bool mG2dProcs;
    //struct g2d_buf* mCurrentBuffer;
    //buffer_handle_t mLastHandle;
    int mSwapIndex;
    hwc_rect_t mSwapRect[HWC_MAX_FRAMEBUFFER];
};

struct fsl_private : public hwc_operations {
    void* g2d_handle;
    struct disp_private mDispInfo[HWC_NUM_DISPLAY_TYPES];
    g2d_buf* tmp_buf;
    int tmp_buf_size;
    bool vg_engine;
};

#endif
