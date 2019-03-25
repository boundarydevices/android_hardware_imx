/*
 * Copyright 2017 NXP.
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

#ifndef _FSL_DISPLAY_MANAGER_H_
#define _FSL_DISPLAY_MANAGER_H_

#include <utils/StrongPointer.h>
#include <hardware_legacy/uevent.h>

#include "VirtualDisplay.h"

#define BUFFER_SIZE 512
#define DRM_DRI_PATH "/dev/dri/"
#define EPOLL_MAX_EVENTS 8

#define MAX_PHYSICAL_DISPLAY 10
#define MAX_VIRTUAL_DISPLAY  16

#define EVENT_MSG_LEN 4096

namespace fsl {

class FbDisplay;
class KmsDisplay;

class DisplayManager
{
public:
    virtual ~DisplayManager();

    static DisplayManager* getInstance();
    Display* getDisplay(int id);
    Display* getPhysicalDisplay(int id);
    VirtualDisplay* getVirtualDisplay(int id);
    VirtualDisplay* createVirtualDisplay();
    int destroyVirtualDisplay(int id);

    bool isOverlay(int fb);
    int enumFbDisplays();
    int enumKmsDisplay(const char *path, int *id, bool *foundPrimary);
    int enumFakeKmsDisplay();
    int enumKmsDisplays();
    void setCallback(EventListener* callback);
    EventListener* getCallback();
    void handleHotplugEvent();
    void handleKmsHotplug();

private:
    void setPrimaryDisplay(int index);
    DisplayManager();
    /* This class mainly handle all uevent in hwc, currently only hdmi
     * hotplugin event needs to be care. */
    class HotplugThread : public Thread {
    public:
        HotplugThread(DisplayManager *ctx);

    private:
        virtual void onFirstRef();
        virtual int32_t readyToRun();
        virtual bool threadLoop();
        virtual bool stringInString(char *uevent_desc, char *sub_string);

        DisplayManager *mCtx;
    };

    sp<HotplugThread> mHotplugThread;

    class PollFileThread : public Thread {
    public:
        PollFileThread(DisplayManager *ctx);

    private:
        virtual void onFirstRef();
        virtual int32_t readyToRun();
        virtual bool threadLoop();

        DisplayManager *mCtx;
        int mINotifyFd;
        int mINotifyWd;
        int mEpollFd;
    };

    sp<PollFileThread> mPollFileThread;

private:
    static Mutex sLock;
    static DisplayManager* sInstance;

    Mutex mLock;
    FbDisplay* mFbDisplays[MAX_PHYSICAL_DISPLAY];
    KmsDisplay* mKmsDisplays[MAX_PHYSICAL_DISPLAY];
    VirtualDisplay* mVirtualDisplays[MAX_VIRTUAL_DISPLAY];
    EventListener* mListener;
    bool mDriverReady;
    bool mDrmMode;
};

}
#endif
