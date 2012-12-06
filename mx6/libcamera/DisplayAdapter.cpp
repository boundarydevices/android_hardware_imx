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

#include "DisplayAdapter.h"

DisplayAdapter::DisplayAdapter()
    : mDisplayThread(NULL),
      mDisplayState(DisplayAdapter::DISPLAY_INVALID),
      mThreadLive(false)
{
    mFrameProvider = NULL;

    mPreviewWidth  = 0;
    mPreviewHeight = 0;
}

DisplayAdapter::~DisplayAdapter()
{
    if (mFrameProvider) {
        mFrameProvider->removeFrameListener(this);
        mFrameProvider = NULL;
    }

    // /The ANativeWindow object will get destroyed here
    destroy();

    // /If Display thread exists
    if (mDisplayThread.get() != NULL) {
        mThreadQueue.postSyncMessage(new SyncMessage(DisplayThread::DISPLAY_EXIT,
                                                     0));
        mDisplayThread->requestExitAndWait();

        mDisplayThread.clear();
    }
}

status_t DisplayAdapter::initialize()
{
    mDisplayThread = new DisplayThread(this);
    if (!mDisplayThread.get()) {
        FLOGE("Couldn't create display thread");
        return NO_MEMORY;
    }

    // /Start the display thread
    mThreadLive = true;
    status_t ret = mDisplayThread->run("DisplayThread", PRIORITY_URGENT_DISPLAY);
    if (ret != NO_ERROR) {
        FLOGE("Couldn't run display thread");
        return ret;
    }

    mDisplayState = DISPLAY_INIT;

    return ret;
}

int DisplayAdapter::setCameraFrameProvider(CameraFrameProvider *frameProvider)
{
    mFrameProvider = frameProvider;
    return NO_ERROR;
}

int DisplayAdapter::startDisplay(int width,
                                 int height)
{
    if (mDisplayState == DisplayAdapter::DISPLAY_STARTED) {
        FLOGW("Display is already started");
        return NO_ERROR;
    }

    if (NULL != mDisplayThread.get()) {
        mThreadQueue.postSyncMessage(new SyncMessage(DisplayThread::DISPLAY_START, 0));
    }

    // Send START_DISPLAY COMMAND to display thread. Display thread will start
    // and then wait for a message

    // Register with the frame provider for frames
    FSL_ASSERT(mFrameProvider);
    mFrameProvider->addFrameListener(this);

    mPreviewWidth   = width;
    mPreviewHeight  = height;

    FLOGI("mPreviewWidth = %d mPreviewHeight = %d", mPreviewWidth, mPreviewHeight);

    return NO_ERROR;
}

int DisplayAdapter::stopDisplay()
{
    status_t ret = NO_ERROR;
    GraphicBufferMapper& mapper = GraphicBufferMapper::get();

    if (mDisplayState == DisplayAdapter::DISPLAY_STOPPED) {
        FLOGW("Display is already stopped");
        return ALREADY_EXISTS;
    }

    // Unregister with the frame provider here
    if (mFrameProvider != NULL) {
        mFrameProvider->removeFrameListener(this);
    }

    if (NULL != mDisplayThread.get()) {
        // Send STOP_DISPLAY COMMAND to display thread. Display thread will stop
        // and dequeue all messages
        // and then wait for message
        mThreadQueue.postSyncMessage(new SyncMessage(DisplayThread::DISPLAY_STOP,
                                                     0));
    }

    Mutex::Autolock lock(mLock);
    {
        // /Reset the frame width and height values
        mFrameWidth    = 0;
        mFrameHeight   = 0;
        mPreviewWidth  = 0;
        mPreviewHeight = 0;
    }

    return ret;
}

bool DisplayAdapter::displayThread()
{
    bool shouldLive = true;

    sp<CMessage> msg = mThreadQueue.waitMessage(THREAD_WAIT_TIMEOUT);
    if (msg == 0) {
        if (mDisplayState == DisplayAdapter::DISPLAY_STARTED) {
            FLOGI("displayThread: get invalid message");
        }
        return true;
    }

    switch (msg->what) {
        case DisplayThread::DISPLAY_FRAME:

            // FLOGI("Display thread received DISPLAY_FRAME command from Camera
            // HAL");
            if (mDisplayState == DisplayAdapter::DISPLAY_INIT) {
                break;
            }
            if (mDisplayState == DisplayAdapter::DISPLAY_STARTED) {
                mThreadLive = requestFrameBuffer();
                if (mThreadLive == false) {
                    FLOGI("display thread dead because of error...");
                    mDisplayState = DisplayAdapter::DISPLAY_EXITED;
                }
            }

            break;

        case DisplayThread::DISPLAY_START:
            FLOGI("Display thread received DISPLAY_START command from Camera HAL");
            if (mThreadLive == false) {
                FLOGI("can't start display thread, thread dead...");
            }
            else {
                mDisplayState = DisplayAdapter::DISPLAY_STARTED;
            }

            break;

        case DisplayThread::DISPLAY_STOP:
            FLOGI("Display thread received DISPLAY_STOP command from Camera HAL");
            if (mThreadLive == false) {
                FLOGI("can't stop display thread, thread dead...");
            }
            else {
                mDisplayState = DisplayAdapter::DISPLAY_STOPPED;
            }

            break;

        case DisplayThread::DISPLAY_EXIT:
            FLOGI("display thread exiting...");
            mDisplayState = DisplayAdapter::DISPLAY_EXITED;

            // /Note that the SF can have pending buffers when we disable the
            // display
            // /This is normal and the expectation is that they may not be
            // displayed.
            // /This is to ensure that the user experience is not impacted
            shouldLive = false;
            break;

        default:
            FLOGE("Invalid Display Thread Command 0x%x.", msg->what);
            break;
    } // end switch

    return shouldLive;
}

bool DisplayAdapter::requestFrameBuffer()
{
    CameraFrame *frame = requestBuffer();

    if (frame == NULL) {
        FLOGE("requestBuffer return null buffer");
        return false;
    }

    // the frame release from SurfaceAdapter.
    frame->release();
    return true;
}

void DisplayAdapter::handleCameraFrame(CameraFrame *frame)
{
    if (!frame || !frame->mBufHandle) {
        FLOGI("DisplayAdapter: notifyCameraFrame receive null frame");
        return;
    }

    // the frame held in SurfaceAdapter.
    frame->addReference();
    if (mDisplayState == DisplayAdapter::DISPLAY_STARTED) {
        Mutex::Autolock lock(mLock);

        renderBuffer(frame->mBufHandle);
    }
    else {
        Mutex::Autolock lock(mLock);

        cancelBuffer(frame->mBufHandle);
    }

    mThreadQueue.postMessage(new CMessage(DisplayThread::DISPLAY_FRAME));
}

