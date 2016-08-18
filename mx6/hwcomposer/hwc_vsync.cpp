/*
 * Copyright (C) 2012-2016 Freescale Semiconductor, Inc. All Rights Reserved.
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
#define VSYNC_STRING_LEN 128

using namespace android;

VSyncThread::VSyncThread(hwc_context_t *ctx)
    : Thread(false), mCtx(ctx), mEnabled(false),
      mFakeVSync(false), mNextFakeVSync(0), mFd(-1)
{
    mRefreshPeriod = 0;
}

void VSyncThread::onFirstRef()
{
    run("HWC-VSYNC-Thread", PRIORITY_URGENT_DISPLAY);
}

status_t VSyncThread::readyToRun()
{
    char fb_path[HWC_PATH_LENGTH];
    memset(fb_path, 0, sizeof(fb_path));
    snprintf(fb_path, HWC_PATH_LENGTH, HWC_FB_SYS"%d""/vsync",
            HWC_DISPLAY_PRIMARY);

    mFd = open(fb_path, O_RDONLY);
    if (mFd <= 0) {
        ALOGW("open %s failed, fallback to fake vsync", fb_path);
        mFakeVSync = true;
    }

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
    char buf[VSYNC_STRING_LEN];
    memset(buf, 0, VSYNC_STRING_LEN);
    static uint64_t lasttime = 0;

    ssize_t len = pread(mFd, buf, VSYNC_STRING_LEN-1, 0);
    if (len < 0) {
        ALOGE("unable to read vsync event error: %s", strerror(errno));
        return;
    }

    if (!strncmp(buf, "VSYNC=", strlen("VSYNC="))) {
        timestamp = strtoull(buf + strlen("VSYNC="), NULL, 0);
    }

    if (lasttime != 0) {
        ALOGV("vsync period: %llu", timestamp - lasttime);
    }

    lasttime = timestamp;
    mCtx->m_callback->vsync(mCtx->m_callback, 0, timestamp);
}

