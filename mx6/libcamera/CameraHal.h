/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2009-2012 Freescale Semiconductor, Inc.
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

#ifndef _CAMERA_HAL_H
#define _CAMERA_HAL_H

#include "CameraUtil.h"
#include "CameraBridge.h"
#include "DeviceAdapter.h"
#include "DisplayAdapter.h"
#include <hardware/camera.h>

using namespace android;

class PhysMemAdapter;

class CameraHal {
public:
    CameraHal(int cameraId);
    ~CameraHal();
    status_t initialize(const CameraInfo& info);

    void     setCallbacks(camera_notify_callback         notify_cb,
                          camera_data_callback           data_cb,
                          camera_data_timestamp_callback data_cb_timestamp,
                          camera_request_memory          get_memory,
                          void                          *user);
    void     enableMsgType(int32_t msgType);
    void     disableMsgType(int32_t msgType);
    bool     msgTypeEnabled(int32_t msgType);
    void     putParameters(char *params);
    char*    getParameters() const;
    status_t setParameters(const char *params);
    status_t setParameters(CameraParameters& params);
    status_t setPreviewWindow(struct preview_stream_ops *window);

    bool     previewEnabled();
    status_t restartPreview();
    status_t startPreview();
    void     stopPreview();
    void     forceStopPreview();

    status_t autoFocus();
    status_t cancelAutoFocus();

    status_t startRecording();
    void     stopRecording();
    void     releaseRecordingFrame(const void *mem);
    status_t storeMetaDataInBuffers(bool enable);
    bool     recordingEnabled();

    status_t takePicture();
    status_t stopPicture();
    status_t cancelPicture();

    status_t sendCommand(int32_t cmd,
                         int32_t arg1,
                         int32_t arg2);
    void     release();
    status_t dump(int fd) const;

    void     LockWakeLock();
    void     UnLockWakeLock();

private:
    sp<CameraBridge>   mCameraBridge;
    sp<DeviceAdapter>  mDeviceAdapter;
    sp<DisplayAdapter> mDisplayAdapter;
    CameraBufferProvider *mBufferProvider;

private:
    bool mPowerLock;
    int  mCameraId;
    mutable Mutex mLock;
    CameraParameters mParameters;
    mutable Mutex    mEncodeLock;
    bool mPreviewEnabled;
    bool mRecordingEnabled;
    bool mTakePictureInProcess;

    bool mSetPreviewWindowCalled;
    bool mPreviewStartInProgress;
    int32_t mMsgEnabled;

    int mSupportedRecordingFormat[MAX_VPU_SUPPORT_FORMAT];
    int mSupportedPictureFormat[MAX_PICTURE_SUPPORT_FORMAT];
    PhysMemAdapter *mPhysAdapter;
    bool mUseIon;
};

#endif // ifndef _CAMERA_HAL_H
