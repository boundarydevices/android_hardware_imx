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
#ifndef SWITCHGOV_H_
#define SWITCHGOV_H_

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <utils/threads.h>
#include <cutils/properties.h>
#include "messageQueue.h"
#define GOV_PATH "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
#define INPUTBOOST_PATH "/sys/devices/system/cpu/cpufreq/interactive/input_boost"
#define PROP_CPUFREQGOV "sys.interactive"
#define PROP_VAL "active"
enum Cpugov {
    INTERACTIVE     =1,
    CONSERVATIVE    =2,
    PERFORMANCE,
    USERSPACE,
    UNKNOW
};

using namespace android;
class SwitchprofileThread : public Thread
{
public:
    SwitchprofileThread();

    void set_cpugov(int gov);
    int do_setproperty(const char *propname, const char *propval);

    enum SwitchcpugovCommands {
        GOV_INTERACTIVE,
        GOV_CONSERVATIVE,
        GOV_PERFORMANCE,
        GOV_USERSPACE
    };

private:
    virtual status_t readyToRun();
    virtual void onFirstRef();
    virtual bool threadLoop();

    bool handle_interactive();
    bool handle_conservative();
    bool handle_performance();
    void do_changecpugov(const char *gov);

    CMessageQueue mThreadQueue;
    mutable Mutex mLock;
    int mFd;
    int mFd1;
    bool mActive;
    static char const* getPath() { return GOV_PATH; }
};
#endif
