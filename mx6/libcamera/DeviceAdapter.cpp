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

#include "DeviceAdapter.h"
#include "UvcDevice.h"
#include "OvDevice.h"

sp<DeviceAdapter> DeviceAdapter::Create(const CameraInfo& info)
{
    sp<DeviceAdapter> devAdapter;
    if(strstr(info.name, UVC_NAME_STRING)) {
        FLOGI("DeviceAdapter: Create uvc device");
        devAdapter = new UvcDevice();
    }
    else {
        FLOGI("DeviceAdapter: Create ov device");
        devAdapter = new OvDevice();
    }
    return devAdapter;
}

DeviceAdapter::DeviceAdapter()
    : mCameraHandle(-1), mQueued(0), mDequeued(0)
{
}

DeviceAdapter::~DeviceAdapter()
{
    // Close the camera handle and free the video info structure
    close(mCameraHandle);

    if(mVideoInfo) {
        delete mVideoInfo;
        mVideoInfo = NULL;
    }
}

status_t DeviceAdapter::initialize(const CameraInfo& info)
{
    if(info.name == NULL) {
        FLOGE("invalid camera sensor name in initialize");
        return BAD_VALUE;
    }
    if(info.devPath == NULL) {
        FLOGE("invalid camera devpath in initialize");
        return BAD_VALUE;
    }

    mCameraHandle = open(info.devPath, O_RDWR);
    if(mCameraHandle < 0) {
        FLOGE("can not open camera devpath:%s", info.devPath);
        return BAD_VALUE;
    }
    mVideoInfo = new VideoInfo();
    if(mVideoInfo == NULL) {
        close(mCameraHandle);
        FLOGE("new VideoInfo failed");
        return NO_MEMORY;
    }

    int ret = NO_ERROR;
    ret = ioctl(mCameraHandle, VIDIOC_QUERYCAP, &mVideoInfo->cap);
    if(ret < 0) {
        close(mCameraHandle);
        delete mVideoInfo;
        FLOGE("query v4l2 capability failed");
        return BAD_VALUE;
    }
    if((mVideoInfo->cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0)
    {
        close(mCameraHandle);
        delete mVideoInfo;
        FLOGE("v4l2 capability does not support capture");
        return BAD_VALUE;
    }

    // Initialize flags
    mPreviewing = false;
    mVideoInfo->isStreamOn = false;
    mImageCapture = false;

    return NO_ERROR;
}

static int getCaptureMode(int width, int height)
{
    int capturemode = 0;

    if(width == 640 && height == 480) {
        capturemode = 0;
    }
    else if(width == 320 && height == 240) {
        capturemode = 1;
    }
    else if(width == 720 && height == 480) {
        capturemode = 2;
    }
    else if(width == 720 && height == 576) {
        capturemode = 3;
    }
    else if(width == 1280 && height == 720) {
        capturemode = 4;
    }
    else if(width == 1920 && height == 1080) {
        capturemode = 5;
    }
    else if(width == 2592 && height == 1944) {
        capturemode = 6;
    }
    else if(width == 176 && height == 144) {
        capturemode = 7;
    }
    else if(width == 1024 && height == 768) {
        capturemode = 8;
    }
    else {
        FLOGE("width:%d height:%d is not supported.", width, height);
    }
    return capturemode;
}

status_t DeviceAdapter::setDeviceConfig(int width, int height, PixelFormat format, int fps)
{
    if(mCameraHandle <= 0) {
        FLOGE("setDeviceConfig: DeviceAdapter uninitialized");
        return BAD_VALUE;
    }
    if(width == 0 || height == 0) {
        FLOGE("setDeviceConfig: invalid parameters");
        return BAD_VALUE;
    }

    status_t ret = NO_ERROR;
    int input = 1;
    ret = ioctl(mCameraHandle, VIDIOC_S_INPUT, &input);
    if (ret < 0) {
        FLOGE("Open: VIDIOC_S_INPUT Failed: %s", strerror(errno));
        return ret;
    }

    int vformat;
    vformat = convertPixelFormatToV4L2Format(format);

    if(width > 1920 || height > 1080) {
        fps = 15;
    }
    FLOGI("Width * Height %d x %d format %d, fps: %d", width, height, vformat, fps);

    mVideoInfo->width = width;
    mVideoInfo->height = height;
    mVideoInfo->framesizeIn = (width * height << 1);
    mVideoInfo->formatIn = vformat;

    mVideoInfo->param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    mVideoInfo->param.parm.capture.timeperframe.numerator = 1;
    mVideoInfo->param.parm.capture.timeperframe.denominator = fps;
    mVideoInfo->param.parm.capture.capturemode = getCaptureMode(width, height);
    ret = ioctl(mCameraHandle, VIDIOC_S_PARM, &mVideoInfo->param);
    if (ret < 0) {
        FLOGE("Open: VIDIOC_S_PARM Failed: %s", strerror(errno));
        return ret;
    }

    mVideoInfo->format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    mVideoInfo->format.fmt.pix.width = width & 0xFFFFFFF8;
    mVideoInfo->format.fmt.pix.height = height & 0xFFFFFFF8;
    mVideoInfo->format.fmt.pix.pixelformat = vformat;
    mVideoInfo->format.fmt.pix.priv = 0;
    mVideoInfo->format.fmt.pix.sizeimage = 0;
    mVideoInfo->format.fmt.pix.bytesperline = 0;

    ret = ioctl(mCameraHandle, VIDIOC_S_FMT, &mVideoInfo->format);
    if (ret < 0) {
        FLOGE("Open: VIDIOC_S_FMT Failed: %s", strerror(errno));
        return ret;
    }

    return ret;
}

int DeviceAdapter::getFrameSize()
{
    return mPreviewBufferSize;
}

int DeviceAdapter::getFrameCount()
{
    return mPreviewBufferCount;
}

status_t DeviceAdapter::registerCameraFrames(CameraFrame* pBuffer, int& num)
{
    status_t ret = NO_ERROR;

    if(pBuffer == NULL || num <= 0) {
        FLOGE("requestCameraBuffers invalid pBuffer");
        return BAD_VALUE;
    }

    mVideoInfo->rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    mVideoInfo->rb.memory = V4L2_MEMORY_USERPTR;
    mVideoInfo->rb.count = num;

    ret = ioctl(mCameraHandle, VIDIOC_REQBUFS, &mVideoInfo->rb);
    if (ret < 0) {
        FLOGE("VIDIOC_REQBUFS failed: %s", strerror(errno));
        return ret;
    }

    for (int i = 0; i < num; i++) {
        CameraFrame* buffer = pBuffer + i;
        memset (&mVideoInfo->buf, 0, sizeof (struct v4l2_buffer));
        mVideoInfo->buf.index = i;
        mVideoInfo->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        mVideoInfo->buf.memory = V4L2_MEMORY_USERPTR;
        mVideoInfo->buf.m.offset = buffer->mPhyAddr;
        mVideoInfo->buf.length = buffer->mSize;

        ret = ioctl (mCameraHandle, VIDIOC_QUERYBUF, &mVideoInfo->buf);
        if (ret < 0) {
            FLOGE("Unable to query buffer (%s)", strerror(errno));
            return ret;
        }
        //Associate each Camera buffer
        buffer->setObserver(this);
        mPreviewBufs.add((int)buffer, i);
    }

    mPreviewBufferSize = pBuffer->mSize;
    mPreviewBufferCount = num;

    return ret;
}

status_t DeviceAdapter::fillCameraFrame(CameraFrame* frame)
{

    status_t ret = NO_ERROR;

    if ( !mVideoInfo->isStreamOn ) {
        return NO_ERROR;
    }

    int i = mPreviewBufs.valueFor(( unsigned int )frame);
    if(i<0) {
        return BAD_VALUE;
    }

    mVideoInfo->buf.index = i;
    mVideoInfo->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    mVideoInfo->buf.memory = V4L2_MEMORY_USERPTR;
    mVideoInfo->buf.m.offset = frame->mPhyAddr;

    ret = ioctl(mCameraHandle, VIDIOC_QBUF, &mVideoInfo->buf);
    if (ret < 0) {
       FLOGE("fillCameraFrame: VIDIOC_QBUF Failed");
       return BAD_VALUE;
    }
    mQueued++;

    return ret;
}

status_t DeviceAdapter::startDeviceLocked()
{
    status_t ret = NO_ERROR;
    FSL_ASSERT(!mPreviewBufs.isEmpty());
    FSL_ASSERT(mBufferProvider != NULL);

    int queueableBufs = mBufferProvider->maxQueueableBuffers();
    FSL_ASSERT(queueableBufs > 0);

    for(int i = 0; i < queueableBufs; i++) {
        CameraFrame* frame = (CameraFrame*)mPreviewBufs.keyAt(i);
        mVideoInfo->buf.index = i;
        mVideoInfo->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        mVideoInfo->buf.memory = V4L2_MEMORY_USERPTR;
        mVideoInfo->buf.m.offset = frame->mPhyAddr;

        ret = ioctl(mCameraHandle, VIDIOC_QBUF, &mVideoInfo->buf);
        if (ret < 0) {
           FLOGE("VIDIOC_QBUF Failed");
           return BAD_VALUE;
        }

        mQueued++;
    }

    enum v4l2_buf_type bufType;
    if(!mVideoInfo->isStreamOn) {
       bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;

       ret = ioctl(mCameraHandle, VIDIOC_STREAMON, &bufType);
       if (ret < 0) {
           FLOGE("VIDIOC_STREAMON failed: %s", strerror(errno));
           return ret;
       }

       mVideoInfo->isStreamOn = true;
    }

    mDeviceThread = new DeviceThread(this);

    FLOGI("Created device thread");
    return ret;
}

status_t DeviceAdapter::stopDeviceLocked()
{
    status_t ret = NO_ERROR;
    enum v4l2_buf_type bufType;

    mDeviceThread->requestExitAndWait();
    mDeviceThread.clear();

    if(mVideoInfo->isStreamOn) {
        bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        ret = ioctl(mCameraHandle, VIDIOC_STREAMOFF, &bufType);
        if (ret < 0) {
            FLOGE("StopStreaming: Unable to stop capture: %s", strerror(errno));
            return ret;
        }

        mVideoInfo->isStreamOn = false;
    }

    mQueued = 0;
    mDequeued = 0;
    mPreviewBufs.clear();

    return ret;
}

status_t DeviceAdapter::startPreview()
{
    status_t ret = NO_ERROR;

    if(mPreviewing) {
        FLOGE("DeviceAdapter: startPreview but preview running");
        return BAD_VALUE;
    }

    Mutex::Autolock lock(mPreviewBufsLock);
    ret = startDeviceLocked();

    mPreviewing = true;

    return ret;
}

status_t DeviceAdapter::stopPreview()
{
    int ret = NO_ERROR;

    if(!mPreviewing) {
        FLOGE("DeviceAdapter: stopPreview but preview not running");
        return NO_INIT;
    }

    Mutex::Autolock lock(mPreviewBufsLock);
    mPreviewing = false;
    ret = stopDeviceLocked();

    return ret;
}

status_t DeviceAdapter::startImageCapture()
{
    status_t ret = NO_ERROR;

    if(mImageCapture) {
        FLOGE("DeviceAdapter: startPreview but preview running");
        return BAD_VALUE;
    }

    Mutex::Autolock lock(mPreviewBufsLock);
    mImageCapture = true;
    ret = startDeviceLocked();

    return ret;
}

status_t DeviceAdapter::stopImageCapture()
{
    int ret = NO_ERROR;

    if(!mImageCapture) {
        FLOGE("DeviceAdapter: stopPreview but preview not running");
        return NO_INIT;
    }

    Mutex::Autolock lock(mPreviewBufsLock);
    mImageCapture = false;
    ret = stopDeviceLocked();

    return ret;
}

CameraFrame* DeviceAdapter::acquireCameraFrame()
{
    int ret;

    mVideoInfo->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    mVideoInfo->buf.memory = V4L2_MEMORY_USERPTR;

    /* DQ */
    ret = ioctl(mCameraHandle, VIDIOC_DQBUF, &mVideoInfo->buf);
    if (ret < 0) {
        FLOGE("GetFrame: VIDIOC_DQBUF Failed");
        return NULL;
    }
    mDequeued++;

    int index = mVideoInfo->buf.index;
    FSL_ASSERT(!mPreviewBufs.isEmpty(), "mPreviewBufs is empty");
    return (CameraFrame*)mPreviewBufs.keyAt(index);
}

//#define FSL_CAMERAHAL_DUMP
static void bufferDump(CameraFrame* frame)
{
#ifdef FSL_CAMERAHAL_DUMP
   //for test code
    char value[100];
    memset(value, 0, sizeof(value));
    static int vflg = 0;
    property_get("rw.camera.test", value, "");
    if(strcmp(value, "1") == 0)
	vflg = 1;
    if(vflg){
	FILE *pf = NULL;
	pf = fopen("/sdcard/camera_tst.data", "wb");
	if(pf == NULL) {
	    FLOGI("open /sdcard/camera_tst.data failed");
	}
	else {
            FLOGI("-----write-----");
	    fwrite(frame->mVirtAddr, frame->mSize, 1, pf);
	    fclose(pf);
	}
	vflg = 0;
    }
#endif
}


int DeviceAdapter::deviceThread()
{
    CameraFrame* frame = NULL;

        frame = acquireCameraFrame();
        if(!frame) {
            if(mQueued - mDequeued <= 0) {
                //if stop preview, then exit.
                if(!mPreviewing) {
                    FLOGI("preview stop, so exit device thread");
                    return BAD_VALUE;
                }
                else {
                    //to check buffer in another cycle.
                    FLOGI("no buffer in v4l driver, check it next time");
                    return NO_ERROR;
                }
            }
            FLOGE("device thread exit with frame = null, %d buffers still in v4l", mQueued - mDequeued);
            if(mErrorListener != NULL) {
                mErrorListener->handleError(ERROR_FATAL);
            }
            return BAD_VALUE;
        }

    if(mImageCapture) {
        sp<CameraEvent> cameraEvt = new CameraEvent();
        cameraEvt->mEventType = CameraEvent::EVENT_SHUTTER;
        dispatchEvent(cameraEvt);

        frame->mFrameType = CameraFrame::IMAGE_FRAME;
    }
    else {
        frame->mFrameType = CameraFrame::PREVIEW_FRAME;
    }

    dispatchCameraFrame(frame);
    if(mImageCapture) {
        FLOGI("device thread exit after take picture");
        return ALREADY_EXISTS;
    }

    return NO_ERROR;
}

status_t DeviceAdapter::autoFocus()
{
    if(mAutoFocusThread != NULL) {
        mAutoFocusThread.clear();
    }

    mAutoFocusThread = new AutoFocusThread(this);
    if(mAutoFocusThread == NULL) {
        return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

status_t DeviceAdapter::cancelAutoFocus()
{
    return NO_ERROR;
}

int DeviceAdapter::autoFocusThread()
{
    sp<CameraEvent> cameraEvt = new CameraEvent();
    cameraEvt->mEventType = CameraEvent::EVENT_FOCUS;
    dispatchEvent(cameraEvt);

    //exit the thread.
    return UNKNOWN_ERROR;
}

void DeviceAdapter::handleFrameRelease(CameraFrame* buffer)
{
    if(mPreviewing) {
        fillCameraFrame(buffer);
    }
}

void DeviceAdapter::setErrorListener(CameraErrorListener* listener)
{
    mErrorListener = listener;
}

void DeviceAdapter::setCameraBufferProvide(CameraBufferProvider* bufferProvider)
{
    if(bufferProvider != NULL) {
        bufferProvider->addBufferListener(this);
    }
    mBufferProvider = bufferProvider;
}

void DeviceAdapter::onBufferCreat(CameraFrame* pBuffer, int num)
{
    registerCameraFrames(pBuffer, num);
}

void DeviceAdapter::onBufferDestroy()
{
    mPreviewBufs.clear();
}
