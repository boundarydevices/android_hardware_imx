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
#ifndef HWC_VSYNC_H_
#define HWC_VSYNC_H_

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
/*****************************************************************************/
#define FB_VSYNC_EVENT "change@/devices/platform/mxc_sdc_fb.0/graphics/fb0"
#define FB_VSYNC_EVENT_PREFIX "change@/devices/platform/mxc_sdc_fb"

using namespace android;

struct hwc_context_t;

class VSyncThread : public Thread
{
public:
    VSyncThread(hwc_context_t *ctx);

private:
    virtual void onFirstRef();
    virtual status_t readyToRun();
    virtual bool threadLoop();
    void handleUevent(const char *buff, int len);

    hwc_context_t *mCtx;
};

#endif
