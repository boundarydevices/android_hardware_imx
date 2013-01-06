/*
 * Copyright (C) 2012-2013 Freescale Semiconductor, Inc. All Rights Reserved.
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
#include "hwc_vsync.h"
/*****************************************************************************/
#define HWC_VIV_HARDWARE_MODULE_ID "hwcomposer_viv"
#define HWC_MAIN_FB "/dev/graphics/fb0"
#define HWC_MAX_FB 6
#define HWC_PATH_LENGTH 256
#define HWC_STRING_LENGTH 32
#define HWC_FB_PATH "/dev/graphics/fb"
#define HWC_FB_SYS "/sys/class/graphics/fb"

class VSyncThread;

enum {
    HWC_DISPLAY_LDB = 1,
    HWC_DISPLAY_HDMI = 2,
    HWC_DISPLAY_DVI = 3
};

typedef struct {
    int fb_num;
    bool connected;
    int type;
    int fd;
    int vsync_period;
    int xres;
    int yres;
    int xdpi;
    int ydpi;
} displayInfo;

struct hwc_context_t {
    hwc_composer_device_1 device;
    /* our private state goes below here */
    displayInfo mDispInfo[HWC_NUM_DISPLAY_TYPES];
    //hwc_layer_list_t* mDispList[HWC_NUM_DISPLAY_TYPES];
    //size_t mListCapacity[HWC_NUM_DISPLAY_TYPES];

    hwc_procs_t* m_callback;
    bool m_vsync_enable;
    sp<VSyncThread> m_vsync_thread;
    hwc_composer_device_1* m_viv_hwc;
    hw_module_t const *m_gralloc_module;
    framebuffer_device_t* mFbDev[HWC_NUM_DISPLAY_TYPES];
    unsigned long mFbPhysAddrs[HWC_NUM_DISPLAY_TYPES];
};

#endif
