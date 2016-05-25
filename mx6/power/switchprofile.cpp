/*
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
/* Copyright (C) 2012,2016 Freescale Semiconductor, Inc. */

#include "switchprofile.h"
#define LOG_TAG "SWITCHPROFILE"
#include <cutils/log.h>

using namespace android;

SwitchprofileThread::SwitchprofileThread()
    : Thread(false)
{
}

status_t SwitchprofileThread::readyToRun()
{
    mFd = open(getPath(), O_WRONLY);
    if (mFd <= 0) {
        ALOGE("Could not open the cpufreq scaling_governor:%s", getPath());
	    return NO_INIT;
    }
    return NO_ERROR;
}

void SwitchprofileThread::onFirstRef()
{
    run("Switchprofilehread", PRIORITY_URGENT_DISPLAY);
}

bool SwitchprofileThread::handle_interactive()
{
    if(mThreadQueue.postMessage(new CMessage(SwitchprofileThread::GOV_INTERACTIVE)) == NO_ERROR)
        return true;
    else
        return false;
}

bool SwitchprofileThread::handle_conservative()
{
    if( mThreadQueue.postMessage(new CMessage(SwitchprofileThread::GOV_CONSERVATIVE))== NO_ERROR)
        return true;
    else
        return false;
}

bool SwitchprofileThread::handle_performance()
{
     return false;
}
int SwitchprofileThread::do_setproperty(const char *propname, const char *propval)
{
    char prop_cpugov[PROPERTY_VALUE_MAX];
    int ret;

    property_set(propname, propval);
    if( property_get(propname, prop_cpugov, NULL) &&
            (strcmp(prop_cpugov, propval) == 0) ){
	    ret = 0;
    }else{
	    ret = -1;
	    ALOGE("setprop: %s = %s fail\n", propname, propval);
    }
	    ALOGE("setprop: %s = %s ", propname, propval);
    return ret;
}

void SwitchprofileThread::do_changecpugov(const char *gov)
{
    Mutex::Autolock lock(mLock);

    if (strncmp("interactive", gov, strlen("interactive")) == 0)
        mActive = true;
    else {
        mActive = false;
        mFd1 = open(INPUTBOOST_PATH, O_WRONLY);
        if (mFd1 <= 0) {
                ALOGE("Could not open cpu gov:%s", INPUTBOOST_PATH);
                return;
        }
        write(mFd1,"0",strlen("0"));
        close(mFd1);
    }
    if (write(mFd, gov, strlen(gov)) < 0){
        ALOGE("Error writing to %s: %s\n", GOV_PATH, strerror(errno));
        return;
    }
    if (mActive){
        do_setproperty(PROP_CPUFREQGOV, PROP_VAL);
        mFd1 = open(INPUTBOOST_PATH, O_WRONLY);
        if (mFd1 <= 0) {
                ALOGE("Could not open cpu gov:%s", INPUTBOOST_PATH);
                return;
        }
        write(mFd1,"1",strlen("1"));
        close(mFd1);
    }

}

void SwitchprofileThread::set_cpugov(int gov)
{
    if(gov >= UNKNOW) {
        ALOGE(" set unsupport cpufreq governor");
        return;
    }
    switch (gov) {
        case INTERACTIVE:
            handle_interactive();
            break;
        case CONSERVATIVE:
            handle_conservative();
            break;
        case PERFORMANCE:
            handle_performance();
            break;
        case USERSPACE:
        default:
            break;
    }
}

bool SwitchprofileThread::threadLoop()
{
    sp<CMessage> msg = mThreadQueue.waitMessage();
    if (msg == 0) {
        ALOGE("swtichprofilehread: get invalid message");
        return false;
    }

    switch (msg->what) {
        case SwitchprofileThread::GOV_INTERACTIVE:
            ALOGI("Switchprofile thread received GOV_INTERACTIVE from Power HAL");
            do_changecpugov("interactive");

            break;

        case SwitchprofileThread::GOV_CONSERVATIVE:
            ALOGI("Switchprofile thread received GOV_CONSERVATIVE from Power HAL");
            do_changecpugov("conservative");

            break;

        case SwitchprofileThread::GOV_PERFORMANCE:
            ALOGI("Switchprofile thread received GOV_PERFORMANCE from Power HAL");
            break;

        default:
            ALOGE("Invalid Switch profile  Thread Command 0x%x.", msg->what);
            break;
    }

    return true;
}
