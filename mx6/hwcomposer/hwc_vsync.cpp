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
#include "hwc_context.h"
#include "hwc_vsync.h"
#include "hwc_display.h"

/*****************************************************************************/

#undef DEBUG_HWC_VSYNC_TIMING

using namespace android;

VSyncThread::VSyncThread(hwc_context_t *ctx)
    : Thread(false), mCtx(ctx), mEnabled(false),
      mFakeVSync(false), mNextFakeVSync(0)
{
    mRefreshPeriod = 0;
}

void VSyncThread::onFirstRef()
{
    run("HWC-VSYNC-Thread", PRIORITY_URGENT_DISPLAY);
}

status_t VSyncThread::readyToRun()
{
    return NO_ERROR;
}

void VSyncThread::setEnabled(bool enabled) {
    Mutex::Autolock _l(mLock);
    mEnabled = enabled;
    mCondition.signal();
}

void VSyncThread::setFakeVSync(bool enable)
{
    mFakeVSync = enable;
}

bool VSyncThread::threadLoop()
{
    { // scope for lock
        Mutex::Autolock _l(mLock);
        while (!mEnabled) {
            mCondition.wait(mLock);
        }
    }

    if (mFakeVSync) {
        performFakeVSync();
    }
    else {
        performVSync();
    }

    return true;
}

void VSyncThread::performFakeVSync()
{
    mRefreshPeriod = mCtx->mDispInfo[HWC_DISPLAY_PRIMARY].vsync_period;
    const nsecs_t period = mRefreshPeriod;
    const nsecs_t now = systemTime(CLOCK_MONOTONIC);
    nsecs_t next_vsync = mNextFakeVSync;
    nsecs_t sleep = next_vsync - now;
    if (sleep < 0) {
        // we missed, find where the next vsync should be
        sleep = (period - ((now - next_vsync) % period));
        next_vsync = now + sleep;
    }
    mNextFakeVSync = next_vsync + period;

    struct timespec spec;
    spec.tv_sec  = next_vsync / 1000000000;
    spec.tv_nsec = next_vsync % 1000000000;

    int err;
    do {
        err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
    } while (err<0 && errno == EINTR);

    if (err == 0) {
        mCtx->m_callback->vsync(mCtx->m_callback, 0, next_vsync);
    }
}

void VSyncThread::performVSync()
{
    uint64_t timestamp = 0;
    uint32_t crt = (uint32_t)&timestamp;

    int err = ioctl(mCtx->mDispInfo[HWC_DISPLAY_PRIMARY].fd,
                    MXCFB_WAIT_FOR_VSYNC, crt);
    if ( err < 0 ) {
        ALOGE("FBIO_WAITFORVSYNC error: %s\n", strerror(errno));
        return;
    }

#ifdef DEBUG_HWC_VSYNC_TIMING
    static nsecs_t last_time_ns;
    nsecs_t cur_time_ns;

    cur_time_ns  = systemTime(SYSTEM_TIME_MONOTONIC);
    mCtx->m_callback->vsync(mCtx->m_callback, 0, timestamp);
    ALOGE("Vsync %llu, %llu\n", cur_time_ns - last_time_ns,
          cur_time_ns - timestamp);
    last_time_ns = cur_time_ns;
#else
    mCtx->m_callback->vsync(mCtx->m_callback, 0, timestamp);
#endif

    struct timespec tm;
    struct timespec ts;
    const nsecs_t wake_up = 400000;

    double m_frame_period_ns = mCtx->mDispInfo[HWC_DISPLAY_PRIMARY].vsync_period;

    ts.tv_nsec =  (timestamp + m_frame_period_ns)
        - (systemTime(SYSTEM_TIME_MONOTONIC) + wake_up );
    ts.tv_sec = 0;
    nanosleep( &ts, &tm);
}

