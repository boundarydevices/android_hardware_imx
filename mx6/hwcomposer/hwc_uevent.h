/*
 * Copyright (C) 2013 Freescale Semiconductor, Inc. All Rights Reserved.
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

#ifndef HWC_UEVENT_H
#define HWC_UEVENT_H

#include <utils/threads.h>
#include <utils/StrongPointer.h>

/* This class mainly handle all uevent in hwc, currently only hdmi
 * hotplugin event needs to be care. */
class UeventThread : public Thread
{
public:
    UeventThread(hwc_context_t *ctx) : Thread(false), mCtx(ctx) {}

private:
    virtual void onFirstRef();
    virtual status_t readyToRun();
    virtual bool threadLoop();
    void handleHdmiUevent(const char *buff, int len);

    hwc_context_t *mCtx;
};


#endif
