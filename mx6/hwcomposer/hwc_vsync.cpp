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

void VSyncThread::handleUevent(const char *buff, int len)
{
    uint64_t timestamp = 0;
    const char *s = buff;

    if(!mCtx || !mCtx->m_callback || !mCtx->m_callback->vsync)
       return;

    s += strlen(s) + 1;

    while(*s) {
        if (!strncmp(s, "VSYNC=", strlen("VSYNC=")))
            timestamp = strtoull(s + strlen("VSYNC="), NULL, 0);

        s += strlen(s) + 1;
        if (s - buff >= len)
            break;
    }

    mCtx->m_callback->vsync(mCtx->m_callback, 0, timestamp);
}

bool VSyncThread::threadLoop()
{
    char uevent_desc[4096];
    memset(uevent_desc, 0, sizeof(uevent_desc));
    int len = uevent_next_event(uevent_desc, sizeof(uevent_desc) - 2);
    const char *pUeventName = FB_VSYNC_EVENT_PREFIX;
    bool vsync = !strncmp(uevent_desc, pUeventName, strlen(pUeventName));
    if(vsync) {
        handleUevent(uevent_desc, len);
    }
    return true;
}


