/*
 * Copyright (C) 2012 Freescale Semiconductor, Inc. All Rights Reserved.
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
#include "gralloc_priv.h"
#include "hwc_vsync.h"
/*****************************************************************************/
#define HWC_MAIN_FB "/dev/graphics/fb0"

class VSyncThread;

struct hwc_context_t {
    hwc_composer_device_t device;
    /* our private state goes below here */

    int m_mainfb_fd;
    float m_mainfb_fps;
    hwc_procs_t* m_callback;
    bool m_vsync_enable;
    sp<VSyncThread> m_vsync_thread;
};

#endif
