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
#define LOG_TAG "WDT"
#include <cutils/log.h>

#include "watchdog.h"

using namespace android;

WatchdogThread::WatchdogThread()
    : Thread(false)
{
}

void WatchdogThread::onFirstRef()
{
    run("watchdogThread", PRIORITY_URGENT_DISPLAY);
}

status_t WatchdogThread::readyToRun()
{
    // open the watchdog device
    mFd = open(getPath(), O_WRONLY);
    if (mFd <= 0) {
        ALOGE("Could not open the watchdog device:%s", getPath());
	return NO_INIT;
    }

    int stout = WDT_TIMEOUT, tout = 0;
    // set the watchdog timeout
    ioctl(mFd, WDIOC_SETTIMEOUT, &stout);
    ALOGD("Setup the watchdog timeout:%d", stout);
    ioctl(mFd, WDIOC_GETTIMEOUT, &tout);
    if (stout != tout) {
        ALOGE("Failed to set the watchdog timeout:%d", tout);
        return BAD_VALUE;
    }
    return NO_ERROR;
}

bool WatchdogThread::threadLoop()
{
    // feed the watchdog
    ioctl(mFd, WDIOC_KEEPALIVE, 0);
    // Sleep 2 secs shorter than feed time, cause the watchdog
    // timer accuracy is 0.5s and we cannot schedule here after
    // 2s, it's gonna be problem to reboot.
    sleep(WDT_TIMEOUT-2);
    return true;
}
