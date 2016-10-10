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
#ifndef HWC_CONTEXT_H_
#define HWC_CONTEXT_H_

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
#include <EGL/egl.h>
#include "hwc_vsync.h"
/*****************************************************************************/
#define HWC_PRIMARY_DISPLAY "/dev/graphics/fb0"
#define HWC_PRIMARY_PATH "/sys/class/graphics/fb0/"
#define HWC_STRING_LENGTH 32

enum {
    HWC_DISPLAY_LDB =  1,
    HWC_DISPLAY_HDMI = 2,
    HWC_DISPLAY_DVI =  3,
    HWC_DISPLAY_LCD =  4,
    HWC_DISPLAY_EPDC = 5
};

class VSyncThread;

struct hwc_context_t {
    hwc_composer_device_1_t device;
    /* our private state goes below here */

    int mFbFile;
    int mBlank;
    int mDisplayType;
    nsecs_t mVsyncPeriod;
    hwc_procs_t* mCallback;
    sp<VSyncThread> mVsyncThread;
};

void hwc_get_display_type(struct hwc_context_t* ctx);
void hwc_get_framebuffer_info(struct hwc_context_t* ctx);

#endif
