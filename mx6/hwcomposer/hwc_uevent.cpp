/*
 * Copyright (C) 2013-2014 Freescale Semiconductor, Inc. All Rights Reserved.
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
#include "hwc_uevent.h"
#include "hwc_display.h"

#define HDMI_PLUG_EVENT "hdmi_video"
#define HDMI_PLUG_CHANGE "change@"
#define HDMI_SII902_PLUG_EVENT "change@/devices/platform/sii902x.0"

using namespace android;



void UeventThread::onFirstRef() {
    run("HWC-UEvent-Thread", PRIORITY_URGENT_DISPLAY);
}

status_t UeventThread::readyToRun() {
    uevent_init();
    return NO_ERROR;
}

void UeventThread::handleHdmiUevent(const char *buff, int len, int dispid) {
    struct private_module_t *priv_m = NULL;
    int fbid = -1;
    const char *s = buff;

    if (!mCtx || !mCtx->m_callback || !mCtx->m_callback->hotplug)
        return;

    s += strlen(s) + 1;

    while (*s) {
        if (!strncmp(s, "EVENT=plugin", strlen("EVENT=plugin"))) {
            if (dispid == HWC_DISPLAY_PRIMARY) {
                mCtx->m_vsync_thread->setFakeVSync(false);
                return;
            }

            mCtx->mDispInfo[HWC_DISPLAY_EXTERNAL].connected = true;
            fbid = hwc_get_display_fbid(mCtx, HWC_DISPLAY_HDMI);
            if (fbid < 0) {
                ALOGE("unrecognized fb num for hdmi");
            } else {
                ALOGI("HDMI Plugin detected");
                hwc_get_framebuffer_info(&mCtx->mDispInfo[HWC_DISPLAY_EXTERNAL]);
                if (mCtx->m_hwc_ops) {
                    mCtx->m_hwc_ops->setDisplayInfo(dispid, mCtx);
                }
            }
        } else if (!strncmp(s, "EVENT=plugout", strlen("EVENT=plugout"))) {
            if (dispid == HWC_DISPLAY_PRIMARY) {
                mCtx->m_vsync_thread->setFakeVSync(true);
                return;
            }

            mCtx->mDispInfo[HWC_DISPLAY_EXTERNAL].connected = false;
            if (mCtx->m_hwc_ops) {
                mCtx->m_hwc_ops->setDisplayInfo(dispid, mCtx);
            }
            ALOGI("HDMI Plugout detected");
        }

        s += strlen(s) + 1;
        if (s - buff >= len)
            break;
    }

    if (fbid >= 0 && mCtx->mFbDev[HWC_DISPLAY_EXTERNAL] == NULL &&
            mCtx->mDispInfo[HWC_DISPLAY_EXTERNAL].connected &&
            mCtx->m_gralloc_module != NULL) {
        ALOGI("HDMI Gralloc Framebuffer opening. ");
        mCtx->mFbDev[HWC_DISPLAY_EXTERNAL] = reinterpret_cast<framebuffer_device_t*>(fbid);
        char fbname[HWC_STRING_LENGTH];
        memset(fbname, 0, sizeof(fbname));
        sprintf(fbname, "fb%d", fbid);
        mCtx->m_gralloc_module->methods->open(mCtx->m_gralloc_module, fbname,
                                              (struct hw_device_t**)&mCtx->mFbDev[HWC_DISPLAY_EXTERNAL]);
        priv_m = (struct private_module_t *)mCtx->m_gralloc_module;
    }

    mCtx->m_callback->hotplug(mCtx->m_callback, HWC_DISPLAY_EXTERNAL,
                              mCtx->mDispInfo[HWC_DISPLAY_EXTERNAL].connected);

    if (mCtx->mFbDev[HWC_DISPLAY_EXTERNAL] != NULL &&
            !mCtx->mDispInfo[HWC_DISPLAY_EXTERNAL].connected &&
            mCtx->m_gralloc_module != NULL) {
        ALOGI("HDMI Gralloc Framebuffer close. ");
        framebuffer_close(mCtx->mFbDev[HWC_DISPLAY_EXTERNAL]);
        mCtx->mFbDev[HWC_DISPLAY_EXTERNAL] = NULL;
    }
}

bool UeventThread::threadLoop() {
    char uevent_desc[4096];
    const char *pSii902 = HDMI_SII902_PLUG_EVENT;

    memset(uevent_desc, 0, sizeof(uevent_desc));
    int len = uevent_next_event(uevent_desc, sizeof(uevent_desc) - 2);
    int type = -1;
    if (strstr(uevent_desc, HDMI_PLUG_EVENT) != NULL &&
         strstr(uevent_desc, HDMI_PLUG_CHANGE) != NULL) {
        type = HWC_DISPLAY_HDMI;
    }
    else if (!strncmp(uevent_desc, pSii902, strlen(pSii902))) {
        type = HWC_DISPLAY_HDMI_ON_BOARD;
    }
    else {
        ALOGV("%s invalid uevent %s", __FUNCTION__, uevent_desc);
        return true;
    }

    int dispid = hwc_get_display_dispid(mCtx, type);
    handleHdmiUevent(uevent_desc, len, dispid);

    return true;
}

