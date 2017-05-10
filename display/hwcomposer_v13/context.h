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
#ifndef HWC_CONTEXT_H_
#define HWC_CONTEXT_H_

#include <fcntl.h>
#include <errno.h>
#include <cutils/log.h>
#include <cutils/atomic.h>
#include <cutils/properties.h>
#include <utils/threads.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <hardware_legacy/uevent.h>
#include <utils/StrongPointer.h>

#include <linux/mxcfb.h>
#include <linux/ioctl.h>
#include <Display.h>

using namespace fsl;

class DisplayListener;

struct hwc_context_t
{
    hwc_composer_device_1 device;
    /* our private state goes below here */
    const hwc_procs_t* m_callback;
    //bool mDeviceComposite[HWC_NUM_DISPLAY_TYPES];
    DisplayListener* mListener;
};

class DisplayListener : public EventListener
{
public:
    DisplayListener(struct hwc_context_t* ctx);
    virtual void onVSync(int disp, nsecs_t timestamp);
    virtual void onHotplug(int disp, bool connected);
    virtual void onRefresh(int disp);

private:
    hwc_context_t* mCtx;
};

#endif
