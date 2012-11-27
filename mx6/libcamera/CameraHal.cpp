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

#include "CameraHal.h"
#include "PhysMemAdapter.h"

using namespace android;

CameraHal::CameraHal(int cameraId)
    : mPowerLock(false), mCameraId(cameraId), mPreviewEnabled(false),
      mRecordingEnabled(false), mTakePictureInProcess(false),
      mSetPreviewWindowCalled(false),
      mPreviewStartInProgress(false), mMsgEnabled(0), mUseIon(true)
{
    if(mUseIon) {
        mPhysAdapter = new PhysMemAdapter();
    }
}

CameraHal::~CameraHal()
{
    release();
    mCameraBridge.clear();
    mDeviceAdapter.clear();
    mDisplayAdapter.clear();
    if(mUseIon && mPhysAdapter) {
        delete mPhysAdapter;
    }
}

status_t CameraHal::initialize(const CameraInfo& info)
{
    status_t ret = NO_ERROR;
    FLOG_RUNTIME("initialize name:%s, path:%s", info.name, info.devPath);
    mDeviceAdapter = DeviceAdapter::Create(info);
    if(mDeviceAdapter == NULL) {
        FLOGE("CameraHal: DeviceAdapter create failed");
        return BAD_VALUE;
    }

    ret = mDeviceAdapter->initialize(info);
    if(ret) {
        FLOGE("CameraHal: DeviceAdapter initialize failed");
        return ret;
    }

    mCameraBridge = new CameraBridge();
    if(mCameraBridge == NULL) {
        FLOGE("CameraHal: new CameraBridge failed");
        return BAD_VALUE;
    }

    ret = mCameraBridge->initialize();
    if(ret) {
        FLOGE("CameraHal: CameraBridge initialize failed");
        return ret;
    }

    mCameraBridge->getSupportedRecordingFormat(mSupportedRecordingFormat, MAX_VPU_SUPPORT_FORMAT);
    mCameraBridge->getSupportedPictureFormat(mSupportedPictureFormat, MAX_PICTURE_SUPPORT_FORMAT);
    ret = mDeviceAdapter->initParameters(mParameters, mSupportedRecordingFormat,
            MAX_VPU_SUPPORT_FORMAT, mSupportedPictureFormat, MAX_PICTURE_SUPPORT_FORMAT);
    if(ret) {
        FLOGE("CameraHal: DeviceAdapter initParameters failed");
        return ret;
    }

    ret = mCameraBridge->initParameters(mParameters);
    if(ret) {
        FLOGE("CameraHal: CameraBridge initParameters failed");
        return ret;
    }

    mDeviceAdapter->setErrorListener(mCameraBridge.get());
    mCameraBridge->setCameraFrameProvider(mDeviceAdapter.get());
    mCameraBridge->setCameraEventProvider(CameraEvent::EVENT_INVALID, mDeviceAdapter.get());
    mBufferProvider = NULL;

    return ret;
}

void CameraHal::setCallbacks(camera_notify_callback notify_cb,
        camera_data_callback data_cb,
        camera_data_timestamp_callback data_cb_timestamp,
        camera_request_memory get_memory,
        void* user)
{
    Mutex::Autolock lock(mLock);
    return mCameraBridge->setCallbacks(notify_cb, data_cb, data_cb_timestamp, get_memory, user);
}

void CameraHal::enableMsgType(int32_t msgType)
{
    if(mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) {
        FLOGI("Enabling Preview Callback");
    }
    else {
        FLOGI("Preview callback not enabled %x", msgType);
    }

    Mutex::Autolock lock(mLock);
    mCameraBridge->enableMsgType(msgType);
    FLOGI("enableMsgType 0x%x", msgType);
    mMsgEnabled |= msgType;
}

void CameraHal::disableMsgType(int32_t msgType)
{
    if(msgType & CAMERA_MSG_PREVIEW_FRAME) {
        FLOGI("Disabling Preview Callback");
    }

    Mutex::Autolock lock(mLock);
    mCameraBridge->disableMsgType(msgType);
    FLOGI("disableMsgType 0x%x", msgType);
    mMsgEnabled &= ~msgType;
}

bool CameraHal::msgTypeEnabled(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    return (mMsgEnabled & msgType);
}

void CameraHal::putParameters(char *params)
{
    free(params);
}

char* CameraHal::getParameters() const
{
    Mutex::Autolock lock(mLock);
    char* params_string;
    String8 params_str8;
    CameraParameters mParams = mParameters;

    params_str8 = mParams.flatten();
    params_string = (char*)malloc(sizeof(char) * (params_str8.length() + 1));
    strcpy(params_string, params_str8.string());
    return params_string;
}

status_t CameraHal::setParameters(const char* params)
{
    CameraParameters parameters;
    String8 str_params(params);

    parameters.unflatten(str_params);
    return setParameters(parameters);
}

status_t CameraHal::setParameters(CameraParameters& params)
{
    status_t ret = NO_ERROR;
    Mutex::Autolock lock(mLock);
    FSL_ASSERT(mDeviceAdapter.get() != NULL);
    ret = mDeviceAdapter->setParameters(params);
    if(ret) {
        FLOGE("CameraHal: initialize mDevice->setParameters failed");
        return ret;
    }

    FSL_ASSERT(mCameraBridge.get() != NULL);
    ret = mCameraBridge->setParameters(params);
    if(ret) {
        FLOGE("CameraHal: initialize mCameraBridge->setParameters failed");
        return ret;
    }

    mParameters = params;
    return ret;
}
status_t CameraHal::sendCommand(int32_t command, int32_t arg1, int32_t arg2)
{
    return BAD_VALUE;
}

status_t CameraHal::setPreviewWindow(struct preview_stream_ops *window)
{
    status_t ret = NO_ERROR;
    FLOG_RUNTIME("setPreviewWindow");
    mSetPreviewWindowCalled = true;

    if(!window) {
        if(mDisplayAdapter.get() != NULL) {
            FLOGI("NULL window passed, destroying display adapter");
            stopPreview();
            mDisplayAdapter.clear();
            mSetPreviewWindowCalled = false;
        }

        FLOGI("NULL ANativeWindow passed to setPreviewWindow");
        return NO_ERROR;
    }
    else if(mDisplayAdapter.get() == NULL) {
        mDisplayAdapter = new DisplayAdapter();
        if(!mDisplayAdapter.get() || ((ret=mDisplayAdapter->initialize())!=NO_ERROR)) {
            if(ret != NO_ERROR) {
                mDisplayAdapter.clear();
                FLOGE("DisplayAdapter initialize failed");
                return ret;
            }
            else {
                FLOGE("Couldn't create DisplayAdapter");
                return NO_MEMORY;
            }
        }

        mDisplayAdapter->setCameraFrameProvider(mDeviceAdapter.get());
        mDeviceAdapter->setCameraBufferProvide(mDisplayAdapter.get());

        mDisplayAdapter->setErrorListener(mCameraBridge.get());

        ret = mDisplayAdapter->setPreviewWindow(window);
        if(ret != NO_ERROR) {
            FLOGE("DisplayAdapter setPreviewWindow returned error %d", ret);
        }

        if(mPreviewStartInProgress) {
            FLOGI("setPreviewWindow called when preview running");
            ret = startPreview();
        }
    }
    else {
        FLOGI("set new preview window but the last one has not been freed");
        ret = mDisplayAdapter->setPreviewWindow(window);
    }

    return ret;

}

status_t CameraHal::startPreview()
{
    FLOG_RUNTIME("startPreview");
    status_t ret = NO_ERROR;
    Mutex::Autolock lock(mLock);

    if(mPreviewEnabled) {
        FLOGE("Preview already running");
        return ALREADY_EXISTS;
    }

    if(mTakePictureInProcess) {
        FLOGI("stop takePicture");
        stopPicture();
    }

    if(!mSetPreviewWindowCalled || (mDisplayAdapter.get() == NULL)) {
        FLOGI("Preview not started. Preview in progress flag set");
        mPreviewStartInProgress = true;
        return NO_ERROR;
    }

    int frameRate = mParameters.getPreviewFrameRate();
    int width, height;
    mParameters.getPreviewSize(&width, &height);

    FSL_ASSERT(mDeviceAdapter.get() != NULL);
    PixelFormat format = mDeviceAdapter->getPreviewPixelFormat();
    mDeviceAdapter->setDeviceConfig(width, height, format, frameRate);

    FSL_ASSERT(mDisplayAdapter.get() != NULL);
    mBufferProvider = mDisplayAdapter.get();
    mDeviceAdapter->setCameraBufferProvide(mBufferProvider);
    ret = mBufferProvider->allocatePreviewBuffer(width, height, format, MAX_PREVIEW_BUFFER);
    if(NO_ERROR != ret) {
        FLOGE("Couldn't allocate buffers for Preview");
        goto error;
    }

    FSL_ASSERT(mCameraBridge.get() != NULL);
    ret = mCameraBridge->start();
    if(ALREADY_EXISTS == ret) {
        FLOGI("mCameraBridge already running");
        ret = NO_ERROR;
    }
    else if(ret) {
        FLOGE("Couldn't start mCameraBridge");
        goto error;
    }

    FLOG_RUNTIME("start display");
    ret = mDisplayAdapter->startDisplay(width, height);
    if(ret != NO_ERROR) {
        FLOGE("Couldn't enable display");
        goto error;
    }

    FLOG_RUNTIME("Starting DeviceAdapter preview mode");
    ret = mDeviceAdapter->startPreview();
    if(ret!=NO_ERROR) {
        FLOGE("Couldn't start preview for DeviceAdapter");
        goto error;
    }
    FLOG_RUNTIME("Started preview");

    mPreviewEnabled = true;
    mPreviewStartInProgress = false;
    LockWakeLock();
    return ret;

error:
    FLOGE("Performing cleanup after error");
    mDeviceAdapter->stopPreview();

    mDisplayAdapter->stopDisplay();
    mBufferProvider->freeBuffer();

    mCameraBridge->stop();
    mBufferProvider = NULL;
    mPreviewStartInProgress = false;
    mPreviewEnabled = false;

    return ret;
}

void CameraHal::stopPreview()
{
    FLOG_RUNTIME("stopPreview");
    Mutex::Autolock lock(mLock);
    if(mTakePictureInProcess && !(mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE)) {
        FLOGI("stop takePicture");
        stopPicture();
    }

    if(!previewEnabled() || mRecordingEnabled) {
        return;
    }

    forceStopPreview();
    UnLockWakeLock();
}

bool CameraHal::previewEnabled()
{
    return (mPreviewEnabled || mPreviewStartInProgress);
}

status_t CameraHal::restartPreview()
{
    status_t ret = NO_ERROR;

    forceStopPreview();

    ret = startPreview();

    return ret;
}

void CameraHal::forceStopPreview()
{
    FLOG_RUNTIME("forceStopPreview");
    if (mDeviceAdapter.get() != NULL) {
        mDeviceAdapter->stopPreview();
    }

    if(mDisplayAdapter.get() != NULL) {
        mDisplayAdapter->stopDisplay();
    }

    if(mCameraBridge.get() != NULL) {
        mCameraBridge->stop();
    }

    if(mBufferProvider != NULL) {
        mBufferProvider->freeBuffer();
    }

    mBufferProvider = NULL;
    mPreviewEnabled = false;
    mPreviewStartInProgress = false;
}

status_t CameraHal::autoFocus()
{
    status_t ret = NO_ERROR;

    Mutex::Autolock lock(mLock);
    mMsgEnabled |= CAMERA_MSG_FOCUS;

    if(mDeviceAdapter != NULL) {
        ret = mDeviceAdapter->autoFocus();
    }
    else {
        ret = BAD_VALUE;
    }

    return ret;
}

status_t CameraHal::cancelAutoFocus()
{
    status_t ret = NO_ERROR;
    Mutex::Autolock lock(mLock);
    mMsgEnabled &= ~CAMERA_MSG_FOCUS;

    if(mDeviceAdapter != NULL) {
        ret = mDeviceAdapter->cancelAutoFocus();
    }

    return ret;
}

status_t CameraHal::storeMetaDataInBuffers(bool enable)
{
    //return mCameraBridge->useMetaDataBufferMode(enable);
    return -1;
}

status_t CameraHal::startRecording()
{
    FLOG_RUNTIME("startRecording");
    if(!previewEnabled()) {
        FLOGE("startRecording: preview not enabled");
        return NO_INIT;
    }

    status_t ret = NO_ERROR;
    mEncodeLock.lock();
    if(mRecordingEnabled == true) {
        FLOGW("%s: Recording is already existed\n", __FUNCTION__);
        mEncodeLock.unlock();
        return ret;
    }

    ret = mCameraBridge->startRecording();
    if(ret) {
        FLOGE("CameraBridge startRecording failed");
        return ret;
    }

    mRecordingEnabled = true;
    mEncodeLock.unlock();

    return NO_ERROR;
}

void CameraHal::stopRecording()
{
    FLOG_RUNTIME("stopRecording");
    mEncodeLock.lock();
    if(mRecordingEnabled) {
        mRecordingEnabled = false;
        mCameraBridge->stopRecording();
    }
    mEncodeLock.unlock();
}

void CameraHal::releaseRecordingFrame(const void* mem)
{
    if(mCameraBridge.get() != NULL) {
        mCameraBridge->releaseRecordingFrame(mem);
    }
}

bool CameraHal::recordingEnabled()
{
    return mRecordingEnabled;
}

status_t CameraHal::takePicture()
{
    FLOG_RUNTIME("takePicture");
    status_t ret = NO_ERROR;
    Mutex::Autolock lock(mLock);

    if(!previewEnabled()) {
        FLOGE("takePicture: preview not start");
        return NO_INIT;
    }

    if(mTakePictureInProcess) {
        FLOGE("takePicture already running");
        return ALREADY_EXISTS;
    }

    forceStopPreview();

    FSL_ASSERT(mCameraBridge.get() != NULL);
    mCameraBridge->initImageCapture();

    int frameRate = mParameters.getPreviewFrameRate();
    int width, height;
    mParameters.getPictureSize(&width, &height);

    FSL_ASSERT(mDeviceAdapter.get() != NULL);
    PixelFormat format = mDeviceAdapter->getPicturePixelFormat();
    mDeviceAdapter->setDeviceConfig(width, height, format, frameRate);

    FSL_ASSERT(mDisplayAdapter.get() != NULL);
    if(mUseIon) {
        FSL_ASSERT(mPhysAdapter);
        mBufferProvider = mPhysAdapter;
        mDeviceAdapter->setCameraBufferProvide(mBufferProvider);
        FLOG_RUNTIME("mPhysAdapter allocatePictureBuffer w:%d, h:%d", width, height);
        ret = mBufferProvider->allocatePictureBuffer(width, height, format, MAX_CAPTURE_BUFFER-1);
    }
    else {
        mBufferProvider = mDisplayAdapter.get();
        mDeviceAdapter->setCameraBufferProvide(mBufferProvider);
        FLOG_RUNTIME("mBufferProvider allocatePictureBuffer w:%d, h:%d", width, height);
        ret = mBufferProvider->allocatePictureBuffer(width, height, format, MAX_CAPTURE_BUFFER);
    }
    if(NO_ERROR != ret) {
        FLOGE("Couldn't allocate buffers for Picture");
        goto error;
    }

    ret = mCameraBridge->start();
    if(ALREADY_EXISTS == ret) {
        FLOGI("mCameraBridge already running");
        ret = NO_ERROR;
    }
    else if(ret) {
        FLOGE("Couldn't start mCameraBridge");
        goto error;
    }

    FLOG_RUNTIME("Starting DeviceAdapter ImageCapture mode");
    ret = mDeviceAdapter->startImageCapture();
    if(ret!=NO_ERROR) {
        FLOGE("Couldn't start ImageCapture w/ DeviceAdapter");
        goto error;
    }
    FLOGI("Started ImageCapture");
    mTakePictureInProcess = true;

    LockWakeLock();
    return ret;

error:
    FLOGE("Performing cleanup after error");
    mDeviceAdapter->stopImageCapture();

    mBufferProvider->freeBuffer();

    mCameraBridge->stop();

    mBufferProvider = NULL;
    mTakePictureInProcess = false;

    return ret;
}

status_t CameraHal::stopPicture()
{
    FLOG_RUNTIME("stopPicture");
    if(!mTakePictureInProcess) {
        FLOGE("takePicture not running");
        return NO_INIT;
    }

    if (mDeviceAdapter.get() != NULL) {
        mDeviceAdapter->stopImageCapture();
    }

    if(mCameraBridge.get() != NULL) {
        mCameraBridge->stop();
    }

    if(mBufferProvider != NULL) {
        mBufferProvider->freeBuffer();
    }

    mBufferProvider = NULL;
    mTakePictureInProcess = false;
    UnLockWakeLock();
    return NO_ERROR;
}

status_t CameraHal::cancelPicture()
{
    FLOG_RUNTIME("cancelPicture");
    status_t ret = stopPicture();
    return ret;
}

void CameraHal::release()
{
    Mutex::Autolock lock(mLock);
    if(mPreviewEnabled) {
        forceStopPreview();
    }

    mSetPreviewWindowCalled = false;
}

void CameraHal::LockWakeLock()
{
    if(!mPowerLock) {
        acquire_wake_lock(PARTIAL_WAKE_LOCK, V4LSTREAM_WAKE_LOCK);
        mPowerLock = true;
    }
}

void CameraHal::UnLockWakeLock()
{
    if(mPowerLock) {
        release_wake_lock(V4LSTREAM_WAKE_LOCK);
        mPowerLock = false;
    }
}

status_t CameraHal::dump(int fd) const
{
    return NO_ERROR;
}

