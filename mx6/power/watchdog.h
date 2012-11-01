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
#ifndef WATCHDOG_H_
#define WATCHDOG_H_

#include <fcntl.h>
#include <errno.h>

#include <utils/threads.h>
#include <linux/watchdog.h>

// set timeout to 30s to make sure system server
// can restarted within enough time when dalvik crash
#define WDT_TIMEOUT	30

using namespace android;

class WatchdogThread : public Thread
{
public:
    WatchdogThread();

private:
    virtual status_t readyToRun();
    virtual void onFirstRef();
    virtual bool threadLoop();
    static char const* getPath() { return "/dev/watchdog"; }
    int mFd;
};

#endif
