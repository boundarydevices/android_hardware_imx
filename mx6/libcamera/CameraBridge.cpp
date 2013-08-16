/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2012-2013 Freescale Semiconductor, Inc.
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

#include "CameraBridge.h"

CameraBridge::CameraBridge()
    : mEventProvider(NULL), mFrameProvider(NULL),
      mThreadLive(false), mUseMetaDataBufferMode(false),
      mNotifyCb(NULL), mDataCb(NULL),
      mDataCbTimestamp(NULL), mRequestMemory(NULL), mCallbackCookie(NULL),
      mMsgEnabled(0), mBridgeState(BRIDGE_INVALID),
      mRecording(false), mVideoWidth(0), mVideoHeight(0),
      mBufferCount(0), mBufferSize(0), mMetaDataBufsSize(0),
      mPreviewMemory(NULL), mVideoMemory(NULL)
{
    memset(mSupprotedThumbnailSizes, 0, sizeof(mSupprotedThumbnailSizes));
    mMetaDataBufsMap.clear();
    mVpuSupportFmt[0] = v4l2_fourcc('N', 'V', '1', '2');
#ifdef EVK_6SL
	mVpuSupportFmt[1] = v4l2_fourcc('Y', 'U', 'Y', 'V');
#else
	mVpuSupportFmt[1] = v4l2_fourcc('Y', 'U', '1', '2');
#endif

}

CameraBridge::~CameraBridge()
{
    if (mFrameProvider != NULL) {
        mFrameProvider->removeFrameListener(this);
    }

    if (mEventProvider != NULL) {
        mEventProvider->removeEventListener(this);
    }

    if (mBridgeThread != NULL) {
        mThreadQueue.postSyncMessage(new SyncMessage(BridgeThread::BRIDGE_EXIT, 0));
        mBridgeThread->requestExitAndWait();
        mBridgeThread.clear();
    }
}

status_t CameraBridge::initialize()
{
    // /Create the app notifier thread
    mBridgeThread = new BridgeThread(this);
    if (!mBridgeThread.get()) {
        FLOGE("Couldn't create bridge thread");
        return NO_MEMORY;
    }

    mThreadLive = true;
    // /Start the display thread
    status_t ret = mBridgeThread->run("BridgeThread", PRIORITY_URGENT_DISPLAY);
    if (ret != NO_ERROR) {
        FLOGE("Couldn't run BridgeThread");
        mBridgeThread.clear();
        return ret;
    }

    mUseMetaDataBufferMode = false;
    mBridgeState           = CameraBridge::BRIDGE_INIT;
    mJpegBuilder           = new JpegBuilder();
    if (mJpegBuilder == NULL) {
        FLOGE("Couldn't create JpegBuilder");
        return NO_MEMORY;
    }
    ret = mJpegBuilder->getSupportedPictureFormat(mPictureSupportFmt,
                                                  MAX_PICTURE_SUPPORT_FORMAT);

    return ret;
}

status_t CameraBridge::getSupportedRecordingFormat(int *pFormat,
                                                   int  len)
{
    if ((pFormat != 0) && (len > 0)) {
        memcpy(pFormat, mVpuSupportFmt, len * sizeof(pFormat[0]));
        return NO_ERROR;
    }

    return BAD_VALUE;
}

status_t CameraBridge::getSupportedPictureFormat(int *pFormat,
                                                 int  len)
{
    if ((pFormat != 0) && (len > 0)) {
        memcpy(pFormat, mPictureSupportFmt, len * sizeof(pFormat[0]));
        return NO_ERROR;
    }

    return BAD_VALUE;
}

status_t CameraBridge::initParameters(CameraParameters& params)
{
    char tmpBuffer[CAMER_PARAM_BUFFER_SIZE];

    /*hard code here*/
    params.set(CameraParameters::KEY_FOCUS_DISTANCES, "24.0,50.0,2147483648.0");
    params.setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
    params.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS,
               CameraParameters::PIXEL_FORMAT_JPEG);
    params.set(CameraParameters::KEY_JPEG_QUALITY, 100);
    strcpy(mSupprotedThumbnailSizes, "0x0,128x128,96x96");
    params.set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES,
               mSupprotedThumbnailSizes);
    params.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, "96");
    params.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, "96");
    params.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, "90");

    memset(tmpBuffer, '\0', sizeof(*tmpBuffer));
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::WHITE_BALANCE_AUTO,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)PARAMS_DELIMITER,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::WHITE_BALANCE_INCANDESCENT,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)PARAMS_DELIMITER,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::WHITE_BALANCE_FLUORESCENT,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)PARAMS_DELIMITER,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::WHITE_BALANCE_DAYLIGHT,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)PARAMS_DELIMITER,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::WHITE_BALANCE_SHADE,
            CAMER_PARAM_BUFFER_SIZE);
    params.set(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE, tmpBuffer);
    params.set(CameraParameters::KEY_WHITE_BALANCE,
               CameraParameters::WHITE_BALANCE_AUTO);

    memset(tmpBuffer, '\0', sizeof(*tmpBuffer));
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::EFFECT_NONE,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)PARAMS_DELIMITER,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::EFFECT_MONO,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)PARAMS_DELIMITER,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::EFFECT_NEGATIVE,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)PARAMS_DELIMITER,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::EFFECT_SOLARIZE,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)PARAMS_DELIMITER,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::EFFECT_SEPIA,
            CAMER_PARAM_BUFFER_SIZE);
    params.set(CameraParameters::KEY_SUPPORTED_EFFECTS, tmpBuffer);
    params.set(CameraParameters::KEY_EFFECT, CameraParameters::EFFECT_NONE);

    memset(tmpBuffer, '\0', sizeof(*tmpBuffer));
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::SCENE_MODE_AUTO,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)PARAMS_DELIMITER,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::SCENE_MODE_PORTRAIT,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)PARAMS_DELIMITER,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::SCENE_MODE_LANDSCAPE,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)PARAMS_DELIMITER,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::SCENE_MODE_SPORTS,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)PARAMS_DELIMITER,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::SCENE_MODE_NIGHT_PORTRAIT,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)PARAMS_DELIMITER,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::SCENE_MODE_FIREWORKS,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)PARAMS_DELIMITER,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::SCENE_MODE_NIGHT,
            CAMER_PARAM_BUFFER_SIZE);
    params.set(CameraParameters::KEY_SUPPORTED_SCENE_MODES, tmpBuffer);
    params.set(CameraParameters::KEY_SCENE_MODE,
               CameraParameters::SCENE_MODE_AUTO);

    params.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES,
               CameraParameters::FOCUS_MODE_AUTO);
    params.set(CameraParameters::KEY_FOCUS_MODE,
               CameraParameters::FOCUS_MODE_AUTO);

    params.set(CameraParameters::KEY_FOCAL_LENGTH, "10.001");
    params.set(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE, "54.8");
    params.set(CameraParameters::KEY_VERTICAL_VIEW_ANGLE, "42.5");
    params.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, "0");
    params.set(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION, "0");
    params.set(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION, "0");
    params.set(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP, "0.0");

    memset(tmpBuffer, '\0', sizeof(*tmpBuffer));
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::ANTIBANDING_50HZ,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)PARAMS_DELIMITER,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::ANTIBANDING_60HZ,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)PARAMS_DELIMITER,
            CAMER_PARAM_BUFFER_SIZE);
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::ANTIBANDING_OFF,
            CAMER_PARAM_BUFFER_SIZE);
    params.set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, tmpBuffer);
    params.set(CameraParameters::KEY_ANTIBANDING,
               CameraParameters::ANTIBANDING_OFF);

    memset(tmpBuffer, '\0', sizeof(*tmpBuffer));
    strncat((char *)tmpBuffer,
            (const char *)CameraParameters::FLASH_MODE_OFF,
            CAMER_PARAM_BUFFER_SIZE);
    params.set(CameraParameters::KEY_SUPPORTED_FLASH_MODES, tmpBuffer);
    params.set(CameraParameters::KEY_FLASH_MODE, CameraParameters::FLASH_MODE_OFF);
    params.set(CameraParameters::KEY_ZOOM_SUPPORTED, "true");

    // params.set(CameraParameters::KEY_ZOOM_SUPPORTED, CameraParameters::TRUE);
    params.set(CameraParameters::KEY_MAX_ZOOM, "1");

    // default zoom should be 0 as CTS defined
    params.set(CameraParameters::KEY_ZOOM, "0");

    // the zoom ratios in 1/100 increments. Ex: a zoom of 3.2x is
    // returned as 320. The number of elements is {@link
    // #getMaxZoom} + 1. The list is sorted from small to large. The
    // first element is always 100. The last element is the zoom
    // ratio of the maximum zoom value.
    params.set(CameraParameters::KEY_ZOOM_RATIOS, "100,200");

    mParameters = params;
    return NO_ERROR;
}

void CameraBridge::setCallbacks(camera_notify_callback         notify_cb,
                                camera_data_callback           data_cb,
                                camera_data_timestamp_callback data_cb_timestamp,
                                camera_request_memory          get_memory,
                                void                          *user)
{
    Mutex::Autolock lock(mLock);

    mNotifyCb        = notify_cb;
    mDataCb          = data_cb;
    mDataCbTimestamp = data_cb_timestamp;
    mRequestMemory   = get_memory;
    mCallbackCookie  = user;
}

status_t CameraBridge::setParameters(CameraParameters& params)
{
    Mutex::Autolock lock(mLock);

    const char *pFlashStr;

    pFlashStr = params.get(CameraParameters::KEY_FLASH_MODE);
    if ((strcmp(pFlashStr,
                CameraParameters::FLASH_MODE_OFF) != 0) &&
        (strcmp(pFlashStr, CameraParameters::FLASH_MODE_AUTO) != 0)
        && (strcmp(pFlashStr,
                   CameraParameters::FLASH_MODE_ON) != 0) &&
        (strcmp(pFlashStr, CameraParameters::FLASH_MODE_RED_EYE) != 0)
        && (strcmp(pFlashStr, CameraParameters::FLASH_MODE_TORCH) != 0)) {
        FLOGE("The flash mode is not corrected");
        return BAD_VALUE;
    }

    const char *pFocusStr;
    pFocusStr = params.get(CameraParameters::KEY_FOCUS_MODE);
    if ((strcmp(pFocusStr,
                CameraParameters::FOCUS_MODE_AUTO) != 0) &&
        (strcmp(pFocusStr, CameraParameters::FOCUS_MODE_INFINITY) != 0)
        && (strcmp(pFocusStr,
                   CameraParameters::FOCUS_MODE_MACRO) != 0) &&
        (strcmp(pFocusStr, CameraParameters::FOCUS_MODE_FIXED) != 0)
        && (strcmp(pFocusStr,
                   CameraParameters::FOCUS_MODE_EDOF) != 0) &&
        (strcmp(pFocusStr,
                CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO) != 0)) {
        FLOGE("The focus mode is not corrected");
        return BAD_VALUE;
    }

    mParameters = params;
    return NO_ERROR;
}

void CameraBridge::setCameraFrameProvider(CameraFrameProvider *frameProvider)
{
    if (frameProvider != NULL) {
        frameProvider->addFrameListener(this);
    }
    mFrameProvider = frameProvider;
}

void CameraBridge::setCameraEventProvider(int32_t              msgs,
                                          CameraEventProvider *eventProvider)
{
    if (eventProvider != NULL) {
        eventProvider->addEventListener(this);
    }
    mEventProvider = eventProvider;
}

status_t CameraBridge::enableMsgType(int32_t msgType)
{
    {
        Mutex::Autolock lock(mLock);
        mMsgEnabled |= msgType;
    }

    return NO_ERROR;
}

status_t CameraBridge::disableMsgType(int32_t msgType)
{
    {
        Mutex::Autolock lock(mLock);
        mMsgEnabled &= ~msgType;
    }

    return NO_ERROR;
}

status_t CameraBridge::useMetaDataBufferMode(bool enable)
{
    mUseMetaDataBufferMode = enable;

    return NO_ERROR;
}

status_t CameraBridge::startRecording()
{
    Mutex::Autolock lock(mRecordingLock);

    if (mRecording) {
        FLOGW("CameraBridge has started Recording");
        return ALREADY_EXISTS;
    }

    status_t ret = allocateVideoBufs();
    mRecording = true;

    return ret;
}

status_t CameraBridge::stopRecording()
{
    Mutex::Autolock lock(mRecordingLock);

    if (!mRecording) {
        FLOGW("CameraBridge has not started Recording");
        return NO_INIT;
    }

    mRecording = false;
    releaseVideoBufs();

    return NO_ERROR;
}

status_t CameraBridge::start()
{
    if (mBridgeState == CameraBridge::BRIDGE_STARTED) {
        FLOGW("CameraBridge already running");
        return ALREADY_EXISTS;
    }

    Mutex::Autolock lock(mLock);
    if (!mFrameProvider) {
        FLOGE("CameraBridge: frameProvider does not initialize");
        return NO_INIT;
    }
    if (!mEventProvider) {
        FLOGE("CameraBridge: eventProvider does not initialize");
        return NO_INIT;
    }

#ifdef EVK_6SL //driver provide yuyv, but h264enc need nv12
    int bufSize = mFrameProvider->getFrameSize() * 3/4;
#else
	int bufSize = mFrameProvider->getFrameSize();
#endif
    int bufCnt  = mFrameProvider->getFrameCount();
    if (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) {
        if (mPreviewMemory != NULL) {
            mPreviewMemory->release(mPreviewMemory);
            mPreviewMemory = NULL;
        }

        mPreviewMemory = mRequestMemory(-1, bufSize, bufCnt, NULL);
        if (mPreviewMemory == NULL) {
            FLOGE("CameraBridge: notifyBufferCreat mRequestMemory failed");
        }
    }

    mBufferSize  = bufSize;
    mBufferCount = bufCnt;

    if (mBridgeThread != NULL) {
        mThreadQueue.postSyncMessage(new SyncMessage(BridgeThread::BRIDGE_START, 0));
    }
    FSL_ASSERT(mFrameProvider.get() != NULL);
    mFrameProvider->addFrameListener(this);

    return NO_ERROR;
}

status_t CameraBridge::stop()
{
    if (mBridgeState == CameraBridge::BRIDGE_STOPPED) {
        FLOGW("CameraBridge already stopped");
        return ALREADY_EXISTS;
    }

    Mutex::Autolock lock(mLock);

    if (mBridgeThread != NULL) {
        mThreadQueue.postSyncMessage(new SyncMessage(BridgeThread::BRIDGE_STOP, 0));
    }

    FSL_ASSERT(mFrameProvider.get() != NULL);
    mFrameProvider->removeFrameListener(this);

    return NO_ERROR;
}

bool CameraBridge::bridgeThread()
{
    bool shouldLive = true;

    sp<CMessage> msg = mThreadQueue.waitMessage(THREAD_WAIT_TIMEOUT);
    if (msg == 0) {
        if (mBridgeState == CameraBridge::BRIDGE_STARTED) {
            FLOGE("bridgeThread: get invalid message");
        }
        return true;
    }

    switch (msg->what) {
        case BridgeThread::BRIDGE_START:
            FLOGI("BridgeThread received BRIDGE_START command from Camera HAL");
            if (mThreadLive == false) {
                FLOGI("can't start bridge thread, thread dead...");
            }
            else {
                mBridgeState = CameraBridge::BRIDGE_STARTED;
            }

            break;

        case BridgeThread::BRIDGE_STOP:
            FLOGI("BridgeThread received BRIDGE_STOP command from Camera HAL");
            if (mThreadLive == false) {
                FLOGI("can't stop bridge thread, thread dead...");
            }
            else {
                mBridgeState = CameraBridge::BRIDGE_STOPPED;
            }

            break;

        case BridgeThread::BRIDGE_EVENT:
            FLOGI("BridgeThread received BRIDGE_EVENT command from Camera HAL");
            if (mBridgeState == CameraBridge::BRIDGE_INIT) {
                break;
            }
            if (mBridgeState == CameraBridge::BRIDGE_STARTED) {
                mThreadLive = processEvent((CameraEvent *)msg->arg0);
                if (mThreadLive == false) {
                    FLOGE("Bridge Thread dead because of error...");
                    mBridgeState = CameraBridge::BRIDGE_EXITED;
                }
            }

            break;

        case BridgeThread::BRIDGE_FRAME:

            // FLOGI("BridgeThread received BRIDGE_FRAME command from Camera
            // HAL");
            if (mBridgeState == CameraBridge::BRIDGE_INIT) {
                break;
            }
            if (mBridgeState == CameraBridge::BRIDGE_STARTED) {
                mThreadLive = processFrame((CameraFrame *)msg->arg0);
                if(mThreadLive == false) {
                    FLOGE("Bridge Thread dead because of error...");
                    mBridgeState = CameraBridge::BRIDGE_EXITED;
                }
            }

            break;

        case BridgeThread::BRIDGE_EXIT:
            mBridgeState = CameraBridge::BRIDGE_EXITED;
            FLOGI("Bridge Thread exiting...");
            shouldLive = false;
            break;
    }

    return shouldLive;
}

bool CameraBridge::processEvent(CameraEvent *event)
{
    // /Receive and send the event notifications to app
    bool ret = true;

    if (NULL == event) {
        FLOGE("CameraBridge: processEvent receive null event");
        return false;
    }

    switch (event->mEventType) {
        case CameraEvent::EVENT_SHUTTER:
            if ((NULL != mNotifyCb) && (mMsgEnabled & CAMERA_MSG_SHUTTER)) {
                mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);
            }

            break;

        case CameraEvent::EVENT_FOCUS:
            if ((NULL != mNotifyCb) && (mMsgEnabled & CAMERA_MSG_FOCUS)) {
                mNotifyCb(CAMERA_MSG_FOCUS, true, 0, mCallbackCookie);
            }

            break;

        default:
            FLOGE("CameraBridge: processEvent does not support event now");
            break;
    }

    event->decStrong(event);
    return ret;
}

bool CameraBridge::processFrame(CameraFrame *frame)
{
    MemoryHeapBase *heap;
    MemoryBase     *buffer = NULL;

    sp<MemoryBase> memBase;
    void *buf = NULL;

    bool ret = true;

    if (!frame || !frame->mBufHandle) {
        FLOGE("CameraBridge: processFrame receive null frame");
        return false;
    }

    if ((frame->mFrameType & CameraFrame::IMAGE_FRAME)) {
        if ((mMsgEnabled & CAMERA_MSG_RAW_IMAGE) && (NULL != mDataCb)) {
            sendRawImageFrame(frame);
        }

        if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE_NOTIFY && (mNotifyCb != NULL)) {
            mNotifyCb(CAMERA_MSG_RAW_IMAGE_NOTIFY, 0, 0, mCallbackCookie);
        }

        if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {
            ret = processImageFrame(frame);
        }
    }
    else if (frame->mFrameType & CameraFrame::PREVIEW_FRAME) {
        if ((mMsgEnabled & CAMERA_MSG_VIDEO_FRAME) &&
            (NULL != mDataCbTimestamp)) {
            sendVideoFrame(frame);
        }

        if ((mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) && (NULL != mDataCb)) {
            sendPreviewFrame(frame);
        }
    }

    // the frame release from CameraBridge.
    frame->release();
    return ret;
}

bool CameraBridge::processImageFrame(CameraFrame *frame)
{
    FSL_ASSERT(frame);
    status_t ret      = NO_ERROR;
    int encodeQuality = 100, thumbQuality = 100;
    int thumbWidth, thumbHeight;
    JpegParams *mainJpeg = NULL, *thumbJpeg = NULL;
    void *rawBuf         = NULL, *thumbBuf = NULL;

    camera_memory_t *rawFrame = mRequestMemory(-1, frame->mSize, 1, NULL);
    if (!rawFrame || !rawFrame->data) {
        FLOGE("CameraBridge:processImageFrame mRequestMemory rawFrame failed");
        return false;
    }
    rawBuf = rawFrame->data;

    camera_memory_t *thumbFrame = mRequestMemory(-1, frame->mSize, 1, NULL);
    if (!thumbFrame || !thumbFrame->data) {
        FLOGE("CameraBridge:processImageFrame mRequestMemory thumbFrame failed");
        return false;
    }
    thumbBuf = thumbFrame->data;

    encodeQuality = mParameters.getInt(CameraParameters::KEY_JPEG_QUALITY);
    if ((encodeQuality < 0) || (encodeQuality > 100)) {
        encodeQuality = 100;
    }

    thumbQuality = mParameters.getInt(
        CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY);
    if ((thumbQuality < 0) || (thumbQuality > 100)) {
        thumbQuality = 100;
    }

    mainJpeg =
        new JpegParams((uint8_t *)frame->mVirtAddr,
                       frame->mSize,
                       (uint8_t *)rawBuf,
                       frame->mSize,
                       encodeQuality,
                       frame->mWidth,
                       frame->mHeight,
                       frame->mWidth,
                       frame->mHeight,
                       mParameters.getPreviewFormat());

    thumbWidth  = mParameters.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
    thumbHeight = mParameters.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);

    if ((thumbWidth > 0) && (thumbHeight > 0)) {
        int thumbSize   = 0;
        int thumbFormat = convertStringToV4L2Format(mParameters.getPreviewFormat());
        switch (thumbFormat) {
            case v4l2_fourcc('N', 'V', '1', '2'):
                thumbSize = thumbWidth * thumbHeight * 3 / 2;
                break;

            case v4l2_fourcc('Y', 'U', '1', '2'):
                thumbSize = thumbWidth * thumbHeight * 3 / 2;
                break;

            case v4l2_fourcc('Y', 'U', 'Y', 'V'):
                thumbSize = thumbWidth * thumbHeight * 2;
                break;

            default:
                FLOGE("Error: format not supported int ion alloc");
                return false;
        }
        thumbSize = frame->mSize;
        thumbJpeg =
            new JpegParams((uint8_t *)frame->mVirtAddr,
                           frame->mSize,
                           (uint8_t *)thumbBuf,
                           thumbSize,
                           thumbQuality,
                           frame->mWidth,
                           frame->mHeight,
                           thumbWidth,
                           thumbHeight,
                           mParameters.getPreviewFormat());
    }

    mJpegBuilder->prepareImage(mParameters);
    ret = mJpegBuilder->encodeImage(mainJpeg, thumbJpeg);
    if (ret != NO_ERROR) {
        FLOGE("CameraBridge:processImageFrame encodeImage failed");
        return false;
    }

    size_t imageSize         = mJpegBuilder->getImageSize();
    camera_memory_t *picture = NULL;
    ret = mJpegBuilder->buildImage(mRequestMemory, &picture);
    if ((ret != NO_ERROR) || !picture) {
        FLOGE("CameraBridge:processImageFrame buildImage failed");
        return false;
    }
    mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, picture, 0, NULL, mCallbackCookie);

    if (mainJpeg) {
        delete mainJpeg;
    }

    if (thumbJpeg) {
        delete thumbJpeg;
    }

    if (rawFrame) {
        rawFrame->release(rawFrame);
        rawFrame = NULL;
    }

    if (thumbFrame) {
        thumbFrame->release(thumbFrame);
        thumbFrame = NULL;
    }

    if (picture) {
        picture->release(picture);
        picture = NULL;
    }

    return true;
}

void CameraBridge::sendRawImageFrame(CameraFrame *frame)
{
    FSL_ASSERT(frame);
    camera_memory_t *RawMemBase = NULL;
    RawMemBase = mRequestMemory(-1, frame->mSize, 1, NULL);
    if (NULL == RawMemBase) {
        FLOGE("CameraBridge: allocateRecordVideoBuf mRequestMemory failed");
        return;
    }

    void *dest = RawMemBase->data;
    if (NULL != dest) {
        void *src = frame->mVirtAddr;
        memcpy(dest, src, frame->mSize);
    }

    mDataCb(CAMERA_MSG_RAW_IMAGE, RawMemBase, 0, NULL, mCallbackCookie);
    RawMemBase->release(RawMemBase);
    RawMemBase = NULL;
}

void CameraBridge::sendPreviewFrame(CameraFrame *frame)
{
    FSL_ASSERT(frame);
    FSL_ASSERT(mPreviewMemory);
    int bufIdx = frame->mIndex;
    FSL_ASSERT(bufIdx >= 0);

    convertNV12toYUV420SP((uint8_t *)(frame->mVirtAddr),
                          (uint8_t *)((unsigned char *)mPreviewMemory->data +
                                      bufIdx * mBufferSize),
                          frame->mWidth, frame->mHeight);
    mDataCb(CAMERA_MSG_PREVIEW_FRAME,
            mPreviewMemory,
            bufIdx,
            NULL,
            mCallbackCookie);
}

void CameraBridge::sendVideoFrame(CameraFrame *frame)
{
    FSL_ASSERT(frame);
    if (!mRecording) {
        FLOGE("CameraBridge: sendVideoFrame but mRecording not enable");
        return;
    }

    mRecordingLock.lock();
    nsecs_t timeStamp = systemTime(SYSTEM_TIME_MONOTONIC);
    int     bufIdx    = frame->mIndex;
    FSL_ASSERT(bufIdx >= 0);
    FSL_ASSERT(mVideoMemory);

    unsigned char *pVideoBuf = (unsigned char *)mVideoMemory->data + bufIdx *
                               mMetaDataBufsSize;
    if (mUseMetaDataBufferMode) {
        VideoMetadataBuffer *pMetaBuf = (VideoMetadataBuffer *)pVideoBuf;
        pMetaBuf->phyOffset = frame->mPhyAddr;
        pMetaBuf->length    = frame->mSize;
    }
    else {
#ifdef EVK_6SL
		convertYUYVtoNV12SP((uint8_t *)frame->mVirtAddr, pVideoBuf, frame->mWidth, frame->mHeight);
#else
        memcpy(pVideoBuf, (void *)frame->mVirtAddr, mMetaDataBufsSize);
#endif
    }

    if (mMetaDataBufsMap.indexOfKey((int)pVideoBuf) >= 0) {
        int fAddr = mMetaDataBufsMap.valueFor((int)pVideoBuf);
        FSL_ASSERT(fAddr == (int)frame);
    }
    else {
        mMetaDataBufsMap.add((int)pVideoBuf, (int)frame);
    }

    // the frame held in mediaRecorder.
    frame->addReference();
    mDataCbTimestamp(timeStamp,
                     CAMERA_MSG_VIDEO_FRAME,
                     mVideoMemory,
                     bufIdx,
                     mCallbackCookie);
    mRecordingLock.unlock();
}

void CameraBridge::releaseRecordingFrame(const void *mem)
{
    CameraFrame *pFrame = (CameraFrame *)mMetaDataBufsMap.valueFor((int)mem);

    // the frame release from mediaRecorder.
    pFrame->release();
}

void CameraBridge::handleCameraFrame(CameraFrame *frame)
{
    if (!frame || !frame->mBufHandle) {
        FLOGI("CameraBridge: notifyCameraFrame receive null frame");
        return;
    }

    // the frame held in CameraBridge.
    frame->addReference();
    mThreadQueue.postMessage(
        new CMessage(BridgeThread::BRIDGE_FRAME, (int)frame));
}

void CameraBridge::handleEvent(sp<CameraEvent>& event)
{
    event->incStrong(event.get());
    mThreadQueue.postMessage(
        new CMessage(BridgeThread::BRIDGE_EVENT, (int)event.get()));
}

status_t CameraBridge::allocateVideoBufs()
{
    if (mVideoMemory != NULL) {
        mVideoMemory->release(mVideoMemory);
        mVideoMemory = NULL;
    }

    if (mUseMetaDataBufferMode) {
        mMetaDataBufsSize = sizeof(VideoMetadataBuffer);
    }
    else {
        mMetaDataBufsSize = mBufferSize;
    }

    mVideoMemory = mRequestMemory(-1, mMetaDataBufsSize, mBufferCount, NULL);
    if (mVideoMemory == NULL) {
        FLOGE("CameraBridge: allocateRecordVideoBuf mRequestMemory failed");
        return NO_MEMORY;
    }

    return NO_ERROR;
}

void CameraBridge::releaseVideoBufs()
{
    if (mVideoMemory != NULL) {
        mVideoMemory->release(mVideoMemory);
        mVideoMemory = NULL;
    }

    mMetaDataBufsMap.clear();
}

status_t CameraBridge::initImageCapture()
{
    mJpegBuilder->reset();
    mJpegBuilder->setParameters(mParameters);
    return NO_ERROR;
}

void CameraBridge::handleError(CAMERA_ERROR err)
{
    if (err == ERROR_FATAL) {
        abort();
        return;
    }

    if ((mNotifyCb != NULL) && (mMsgEnabled & CAMERA_MSG_ERROR)) {
        mNotifyCb(CAMERA_MSG_ERROR, CAMERA_ERROR_UNKNOWN, 0, mCallbackCookie);
    }
}

void CameraBridge::convertNV12toYUV420SP(uint8_t *inputBuffer,
                                         uint8_t *outputBuffer,
                                         int      width,
                                         int      height)
{
    /* Color space conversion from I420 to YUV420SP */
    int Ysize = 0, UVsize = 0;
    uint8_t *Yin, *Uin, *Vin, *Yout, *Uout, *Vout;

    Ysize  = width * height;
    UVsize = width *  height >> 2;

    Yin = inputBuffer;
    Uin = Yin + Ysize;
    Vin = Uin + 1;

    Yout = outputBuffer;
    Vout = Yout + Ysize;
    Uout = Vout + 1;

    memcpy(Yout, Yin, Ysize);

    for (int k = 0; k < UVsize; k++) {
        *Uout = *Uin;
        *Vout = *Vin;
        Uout += 2;
        Vout += 2;
        Uin  += 2;
        Vin  += 2;
    }
}


void CameraBridge::convertYUYVtoNV12SP(uint8_t *inputBuffer,
                                         uint8_t *outputBuffer,
                                         int      width,
                                         int      height)
{
#define u32 unsigned int
#define u8 unsigned char

    u32 h,w;
    u32 nHeight = height;
    u32 nWidthDiv4  = width/4;

    u8* pYSrcOffset = inputBuffer;
    u8* pUSrcOffset = inputBuffer + 1;
    u8* pVSrcOffset = inputBuffer + 3;

    u32* pYDstOffset = (u32*)outputBuffer;
    u32* pUVDstOffset = (u32*)(((u8*)(outputBuffer)) + width*height);


    for(h=0; h<nHeight; h++) {
       if(!( h & 0x1 )) {
           for(w=0; w<nWidthDiv4; w++) {
               *pYDstOffset = (((u32)(*(pYSrcOffset+0)))<<0)  +  (((u32)(*(pYSrcOffset+2)))<<8) + (((u32)(*(pYSrcOffset+4)))<<16) + (((u32)(*(pYSrcOffset+6)))<<24) ;
               pYSrcOffset += 8;
               pYDstOffset += 1;
               //*pUVDstOffset = (((u32)(*(pUSrcOffset+0)))<<0)  + (((u32)(*(pVSrcOffset+0)))<<8) + (((u32)(*(pUSrcOffset+4)))<<16) + (((u32)(*(pVSrcOffset+4)))<<24) ;
			   //maybe th encoder use VUVU planner
               *pUVDstOffset = (((u32)(*(pVSrcOffset+0)))<<0)  + (((u32)(*(pUSrcOffset+0)))<<8) + (((u32)(*(pVSrcOffset+4)))<<16) + (((u32)(*(pUSrcOffset+4)))<<24) ;
               pUSrcOffset += 8;
               pVSrcOffset += 8;
               pUVDstOffset += 1;
           }
       } else {
           pUSrcOffset += nWidthDiv4*8;
           pVSrcOffset += nWidthDiv4*8;
           for(w=0; w<nWidthDiv4; w++) {
               *pYDstOffset = (((u32)(*(pYSrcOffset+0)))<<0)  +  (((u32)(*(pYSrcOffset+2)))<<8) + (((u32)(*(pYSrcOffset+4)))<<16) + (((u32)(*(pYSrcOffset+6)))<<24) ;
               pYSrcOffset += 8;
               pYDstOffset += 1;
           }
       }
    }
}
