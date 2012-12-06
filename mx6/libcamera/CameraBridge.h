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

#ifndef _CAMERA_BRIDGE_H_
#define _CAMERA_BRIDGE_H_

#include "CameraUtil.h"
#include <hardware/camera.h>
#include "messageQueue.h"
#include "JpegBuilder.h"

using namespace android;

#define MAX_THUMBNAIL_BUFFER_SIZE 32

#define MAX_VPU_SUPPORT_FORMAT 2
#define MAX_PICTURE_SUPPORT_FORMAT 2

class CameraBridge : public CameraEventListener,
                     public CameraFrameListener,
                     public CameraErrorListener,
                     public LightRefBase<CameraBridge>{
public:
    enum BridgeState {
        BRIDGE_INVALID = 0,
        BRIDGE_INIT    = 1,
        BRIDGE_STARTED,
        BRIDGE_STOPPED,
        BRIDGE_EXITED
    };

    CameraBridge();
    virtual ~CameraBridge();
    status_t initialize();
    status_t start();
    status_t stop();

    status_t initImageCapture();
    status_t getSupportedRecordingFormat(int *pFormat,
                                         int  len);
    status_t getSupportedPictureFormat(int *pFormat,
                                       int  len);

    status_t enableMsgType(int32_t msgType);
    status_t disableMsgType(int32_t msgType);

    status_t startRecording();
    status_t stopRecording();
    status_t useMetaDataBufferMode(bool enable);
    void     releaseRecordingFrame(const void *mem);

public:
    void     setCallbacks(camera_notify_callback         notify_cb,
                          camera_data_callback           data_cb,
                          camera_data_timestamp_callback data_cb_timestamp,
                          camera_request_memory          get_memory,
                          void                          *user);

    virtual status_t setParameters(CameraParameters& params);
    virtual status_t initParameters(CameraParameters& params);

    void             setCameraFrameProvider(CameraFrameProvider *frameProvider);
    void             setCameraEventProvider(int32_t              msgs,
                                            CameraEventProvider *eventProvider);

protected:
    void         handleCameraFrame(CameraFrame *frame);
    void         handleEvent(sp<CameraEvent>& event);
    void         handleError(CAMERA_ERROR err);

    virtual bool processImageFrame(CameraFrame *frame);

private:
    bool         bridgeThread();
    bool         processEvent(CameraEvent *event);
    bool         processFrame(CameraFrame *frame);

    void         sendPreviewFrame(CameraFrame *frame);
    void         sendVideoFrame(CameraFrame *frame);
    void         sendRawImageFrame(CameraFrame *frame);

    status_t     allocateVideoBufs();
    void         releaseVideoBufs();
    void         convertNV12toYUV420SP(uint8_t *inputBuffer,
                                       uint8_t *outputBuffer,
                                       int      width,
                                       int      height);

public:
    class BridgeThread : public Thread {
    public:
        enum BridgeCommand {
            BRIDGE_START,
            BRIDGE_STOP,
            BRIDGE_EVENT,
            BRIDGE_FRAME,
            BRIDGE_EXIT,
        };

    public:
        BridgeThread(CameraBridge *camera)
            : Thread(false), mCamera(camera)
        {}

        virtual bool threadLoop() {
            return mCamera->bridgeThread();
        }

    private:
        CameraBridge *mCamera;
    };

private:
    sp<BridgeThread> mBridgeThread;
    CMessageQueue    mThreadQueue;
    CameraEventProvider *mEventProvider;
    CameraFrameProvider *mFrameProvider;
    bool mThreadLive;

private:
    bool mUseMetaDataBufferMode;
    CameraParameters mParameters;
    camera_notify_callback mNotifyCb;
    camera_data_callback   mDataCb;
    camera_data_timestamp_callback mDataCbTimestamp;
    camera_request_memory mRequestMemory;
    void *mCallbackCookie;

private:
    mutable Mutex mLock;
    int32_t mMsgEnabled;
    char    mSupprotedThumbnailSizes[MAX_THUMBNAIL_BUFFER_SIZE];

    BridgeState   mBridgeState;
    mutable Mutex mRecordingLock;
    bool mRecording;
    int  mVideoWidth;
    int  mVideoHeight;

    int mBufferCount;
    int mBufferSize;
    int mMetaDataBufsSize;
    camera_memory_t *mPreviewMemory;
    camera_memory_t *mVideoMemory;
    KeyedVector<int, int> mMetaDataBufsMap;

    int mVpuSupportFmt[MAX_VPU_SUPPORT_FORMAT];
    int mPictureSupportFmt[MAX_PICTURE_SUPPORT_FORMAT];
    sp<JpegBuilder> mJpegBuilder;
};

#endif // ifndef _CAMERA_BRIDGE_H_
