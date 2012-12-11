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
#include "hwc_context.h"
#include "hwc_vsync.h"

/*****************************************************************************/

using namespace android;

extern int hwc_get_display_fbid(struct hwc_context_t* ctx, int disp_type);
extern int hwc_get_framebuffer_info(displayInfo *pInfo);

VSyncThread::VSyncThread(hwc_context_t *ctx)
    : Thread(false), mCtx(ctx)
{
}

void VSyncThread::onFirstRef()
{
    run("vsyncThread", PRIORITY_URGENT_DISPLAY);
}

status_t VSyncThread::readyToRun()
{
    uevent_init();
    return NO_ERROR;
}

void VSyncThread::handleVsyncUevent(const char *buff, int len)
{
    uint64_t timestamp = 0;
    const char *s = buff;

    if (!mCtx || !mCtx->m_callback || !mCtx->m_callback->vsync)
       return;

    s += strlen(s) + 1;

    while (*s) {
        if (!strncmp(s, "VSYNC=", strlen("VSYNC=")))
            timestamp = strtoull(s + strlen("VSYNC="), NULL, 0);

        s += strlen(s) + 1;
        if (s - buff >= len)
            break;
    }

    mCtx->m_callback->vsync(mCtx->m_callback, 0, timestamp);
}

void VSyncThread::handleHdmiUevent(const char *buff, int len)
{
    if (!mCtx || !mCtx->m_callback || !mCtx->m_callback->hotplug)
        return;

    int fbid = -1;
    const char *s = buff;
    s += strlen(s) + 1;

    while (*s) {
        if (!strncmp(s, "EVENT=plugin", strlen("EVENT=plugin"))) {
            mCtx->mDispInfo[HWC_DISPLAY_EXTERNAL].connected = true;
            fbid = hwc_get_display_fbid(mCtx, HWC_DISPLAY_HDMI);
            if (fbid < 0) {
                ALOGE("unrecognized fb num for hdmi");
            }
            else {
                ALOGI("-----------hdmi---plug--in----");
                if (mCtx->mDispInfo[HWC_DISPLAY_EXTERNAL].xres == 0) {
                    hwc_get_framebuffer_info(&mCtx->mDispInfo[HWC_DISPLAY_EXTERNAL]);
                }
            }
        }
        else if (!strncmp(s, "EVENT=plugout", strlen("EVENT=plugout"))) {
            mCtx->mDispInfo[HWC_DISPLAY_EXTERNAL].connected = false;
            ALOGI("-----------hdmi---plug--out----");
        }

        s += strlen(s) + 1;
        if (s - buff >= len)
            break;
    }

    if (fbid >= 0 && mCtx->mFbDev[HWC_DISPLAY_EXTERNAL] == NULL && mCtx->m_gralloc_module != NULL) {
        ALOGI("-----------hdmi---open framebuffer----");
        mCtx->mFbDev[HWC_DISPLAY_EXTERNAL] = (framebuffer_device_t*)fbid;
        char fbname[HWC_STRING_LENGTH];
        memset(fbname, 0, sizeof(fbname));
        sprintf(fbname, "fb%d", fbid);
        mCtx->m_gralloc_module->methods->open(mCtx->m_gralloc_module, fbname,
                     (struct hw_device_t**)&mCtx->mFbDev[HWC_DISPLAY_EXTERNAL]);
    }

    mCtx->m_callback->hotplug(mCtx->m_callback, HWC_DISPLAY_EXTERNAL, 
                      mCtx->mDispInfo[HWC_DISPLAY_EXTERNAL].connected);
}

bool VSyncThread::threadLoop()
{
    char uevent_desc[4096];
    memset(uevent_desc, 0, sizeof(uevent_desc));
    int len = uevent_next_event(uevent_desc, sizeof(uevent_desc) - 2);
    const char *pVsyncEvent = FB_VSYNC_EVENT_PREFIX;
    const char *pHdmiEvent = HDMI_PLUG_EVENT;
    bool vsync = !strncmp(uevent_desc, pVsyncEvent, strlen(pVsyncEvent));
    bool hdmi = !strncmp(uevent_desc, pHdmiEvent, strlen(pHdmiEvent));
    if(vsync) {
        handleVsyncUevent(uevent_desc, len);
    }
    else if(hdmi) {
        handleHdmiUevent(uevent_desc, len);
    }

    return true;
}


