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

#ifndef _DEVICE_ADAPTER_H_
#define _DEVICE_ADAPTER_H_

#include "CameraUtil.h"

using namespace android;

class DeviceAdapter : public CameraFrameProvider, public CameraBufferListener,
        public CameraEventProvider,
        public CameraFrameObserver, public LightRefBase<DeviceAdapter>
{
public:
    static sp<DeviceAdapter> Create(const CameraInfo& info);
    DeviceAdapter();
    ~DeviceAdapter();

public:
    virtual int getFrameSize();
    virtual int getFrameCount();

    void setErrorListener(CameraErrorListener* listener);
    void setCameraBufferProvide(CameraBufferProvider* bufferProvider);
    virtual status_t initialize(const CameraInfo& info);
    status_t setDeviceConfig(int width, int height, PixelFormat format, int fps);
    PixelFormat getPreviewPixelFormat() {return mPicturePixelFormat;}
    PixelFormat getPicturePixelFormat() {return mPreviewPixelFormat;}

    virtual status_t setParameters(CameraParameters& params) = 0;
    virtual status_t initParameters(CameraParameters& params, int* supportRecordingFormat, int rfmtLen,
                        int* supportPictureFormat, int pfmtLen) = 0;

    //API to send a command to the camera
    //virtual status_t sendCommand(CameraCommands operation, int value1 = 0, int value2 = 0, int value3 = 0 );
    status_t autoFocus();
    status_t cancelAutoFocus();

    virtual status_t startPreview();
    virtual status_t stopPreview();

    virtual status_t startImageCapture();
    virtual status_t stopImageCapture();

protected:
    void onBufferCreat(CameraFrame* pBuffer, int num);
    void onBufferDestroy();
    virtual status_t registerCameraFrames(CameraFrame* pBuffer, int& num);
    virtual void handleFrameRelease(CameraFrame* buffer);

private:
    class AutoFocusThread : public Thread {
    public:
        AutoFocusThread(DeviceAdapter* hw) :
                Thread(false), mAdapter(hw) { }

        virtual void onFirstRef() {
            run("AutoFocusThread", PRIORITY_URGENT_DISPLAY);
        }

        virtual bool threadLoop() {
            int ret = 0;
            ret = mAdapter->autoFocusThread();
            if(ret != 0) {
                return false;
            }
            // loop until we need to quit
            return true;
        }

    private:
        DeviceAdapter* mAdapter;
    };

    class DeviceThread : public Thread {
    public:
        DeviceThread(DeviceAdapter* hw) :
                Thread(false), mAdapter(hw) { }

        virtual void onFirstRef() {
            run("DeviceThread", PRIORITY_URGENT_DISPLAY);
        }

        virtual bool threadLoop() {
            int ret = 0;
            ret = mAdapter->deviceThread();
            if(ret != 0) {
                return false;
            }
            // loop until we need to quit
            return true;
        }

    private:
        DeviceAdapter* mAdapter;
    };

private:
    status_t fillCameraFrame(CameraFrame* frame);
    CameraFrame* acquireCameraFrame();

    status_t startDeviceLocked();
    status_t stopDeviceLocked();
    int deviceThread();
    int autoFocusThread();

protected:
    CameraBufferProvider* mBufferProvider;
    CameraErrorListener* mErrorListener;
    int mPreviewBufferCount;
    int mPreviewBufferSize;
    KeyedVector<int, int> mPreviewBufs;
    mutable Mutex mPreviewBufsLock;

    mutable Mutex mLock;
    CameraParameters mParams;
    bool mPreviewing;
    bool mImageCapture;
    sp<DeviceThread> mDeviceThread;
    sp<AutoFocusThread> mAutoFocusThread;

    struct VideoInfo *mVideoInfo;
    int mCameraHandle;
    int mQueued;
    int mDequeued;

    PixelFormat mPicturePixelFormat;
    PixelFormat mPreviewPixelFormat;
};

#endif
