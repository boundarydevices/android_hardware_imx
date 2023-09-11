/*
 * Copyright 2017 NXP.
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
#ifndef HWC2_CONTEXT_H_
#define HWC2_CONTEXT_H_

#include <Display.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer2.h>

using namespace fsl;

class DisplayListener;

struct hwc2_context_t {
    hwc2_device_t device;

    DisplayListener* mListener;
    HWC2_PFN_HOTPLUG mHotplug;
    hwc2_callback_data_t mHotplugData;
    HWC2_PFN_VSYNC mVsync;
    hwc2_callback_data_t mVsyncData;
    HWC2_PFN_REFRESH mRefresh;
    hwc2_callback_data_t mRefreshData;
    HWC2_PFN_VSYNC_2_4 mVsync_2_4;
    hwc2_callback_data_t mVsyncData_2_4;
    HWC2_PFN_VSYNC_PERIOD_TIMING_CHANGED mVsyncPeriodTimingChanged;
    hwc2_callback_data_t mVsyncPeriodTimingChangedData;
    HWC2_PFN_SEAMLESS_POSSIBLE mSeamlessPossible;
    hwc2_callback_data_t mSeamlessPossibleData;
    bool checkHDMI;
    bool color_tranform;
};

class DisplayListener : public EventListener {
public:
    DisplayListener(struct hwc2_context_t* ctx);
    virtual void onVSync(int disp, nsecs_t timestamp, int vsyncPeriodNanos);
    virtual void onHotplug(int disp, bool connected);
    virtual void onRefresh(int disp);
    virtual void onVSyncPeriodTimingChanged(int disp, nsecs_t newVsyncAppliedTimeNanos,
                                            bool refreshRequired, nsecs_t refreshTimeNanos);
    virtual void onSeamlessPossible(int disp);

private:
    hwc2_context_t* mCtx;
};

#endif
