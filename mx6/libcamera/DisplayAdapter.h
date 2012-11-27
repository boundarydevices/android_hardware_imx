/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
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

#ifndef _DISPLAY_ADAPTER_H_
#define _DISPLAY_ADAPTER_H_

#include "CameraUtil.h"
#include "SurfaceAdapter.h"
#include "messageQueue.h"

using namespace android;

class DisplayAdapter : public SurfaceAdapter, public CameraFrameListener
{
public:
    enum DisplayStates {
        DISPLAY_INVALID = 0,
        DISPLAY_INIT = 1,
        DISPLAY_STARTED,
        DISPLAY_STOPPED,
        DISPLAY_EXITED
    };

    DisplayAdapter();
    virtual ~DisplayAdapter();

    virtual status_t initialize();

    virtual int startDisplay(int width, int height);
    virtual int stopDisplay();

    int setCameraFrameProvider(CameraFrameProvider* frameProvider);

protected:
    void handleCameraFrame(CameraFrame* frame);
    bool displayThread();

private:
    bool requestFrameBuffer();

public:
    class DisplayThread : public Thread {
    public:
        DisplayThread(DisplayAdapter* da)
                 : Thread(false), mDisplayAdapter(da) { }

        virtual bool threadLoop()
        {
            return mDisplayAdapter->displayThread();
        }

        enum DisplayThreadCommands {
            DISPLAY_START,
            DISPLAY_STOP,
            DISPLAY_FRAME,
            DISPLAY_EXIT
        };

        private:
            DisplayAdapter* mDisplayAdapter;
    };

friend class DisplayThread;

private:
    int postBuffer(void* displayBuf);

private:
    uint32_t mPreviewWidth;
    uint32_t mPreviewHeight;
    CameraFrameProvider* mFrameProvider;

    sp<DisplayThread> mDisplayThread;

    CMessageQueue mThreadQueue;
    unsigned int mDisplayState;

    mutable Mutex mLock;
    bool mDisplayEnabled;
};

#endif
