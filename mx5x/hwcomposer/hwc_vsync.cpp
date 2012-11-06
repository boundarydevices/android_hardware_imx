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
#include <sys/time.h>
#include <cutils/log.h>

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
    run("vsyncThread", PRIORITY_URGENT_DISPLAY + ANDROID_PRIORITY_LESS_FAVORABLE);
}

void VSyncThread::setEnabled(bool enabled) {
    Mutex::Autolock _l(mLock);
    mEnabled = enabled;
    mCondition.signal();
}

status_t VSyncThread::readyToRun()
{
    return NO_ERROR;
}

bool VSyncThread::threadLoop()
{
    { // scope for lock
        Mutex::Autolock _l(mLock);
        while (!mEnabled) {
            mCondition.wait(mLock);
        }
    }

    uint64_t timestamp = 0;
    uint32_t crt = (uint32_t)&timestamp;

    int err = ioctl(mCtx->m_mainfb_fd, MXCFB_WAIT_FOR_VSYNC, crt);
    if ( err < 0 ) {
        ALOGE("FBIO_WAITFORVSYNC error: %s\n", strerror(errno));
    } else {
#ifdef DEBUG_HWC_VSYNC_TIMING
        static nsecs_t last_time_ns;
        nsecs_t cur_time_ns;

        cur_time_ns  = systemTime(SYSTEM_TIME_MONOTONIC);
        mCtx->m_callback->vsync(mCtx->m_callback, 0, timestamp);
        ALOGE("Vsync %llu, %llu\n", cur_time_ns - last_time_ns, cur_time_ns - timestamp);
        last_time_ns = cur_time_ns;
#else
        mCtx->m_callback->vsync(mCtx->m_callback, 0, timestamp);
#endif
        {
            struct timespec tm;
            struct timespec ts;
            const nsecs_t wake_up = 400000;

            ts.tv_nsec =  (timestamp + mCtx->m_frame_period_ns) - (systemTime(SYSTEM_TIME_MONOTONIC) + wake_up );
            ts.tv_sec = 0;
            nanosleep( &ts, &tm);
        }
    }
    return true;
}

