/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2012-2014 Freescale Semiconductor, Inc.
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
#include "Ov5640.h"
#include "Ov5642.h"
#include "TVINDevice.h"

sp<DeviceAdapter>DeviceAdapter::Create(const CameraInfo& info)
{
    sp<DeviceAdapter> devAdapter;
    if (strstr(info.name, UVC_SENSOR_NAME)) {
        FLOGI("DeviceAdapter: Create uvc device");
        devAdapter = new UvcDevice();
    }
#ifdef VADC_TVIN
    else if (strstr(info.name, VADC_TVIN_NAME)) {
        FLOGI("DeviceAdapter: Create tvin device");
        devAdapter = new TVINDevice();
    }
#endif
    else if (strstr(info.name, OV5640_SENSOR_NAME)) {
        FLOGI("DeviceAdapter: Create ov5640 device");
        devAdapter = new Ov5640();
    }
    else if (strstr(info.name, OV5642_SENSOR_NAME)) {
        FLOGI("DeviceAdapter: Create ov5642 device");
        devAdapter = new Ov5642();
    }else if (strstr(info.name, ADV7180_TVIN_NAME)) {
        FLOGI("DeviceAdapter: Create adv7180 device");
        devAdapter = new TVINDevice();
    }
    else {
        devAdapter = new OvDevice();
        FLOGI("sensor %s does not support well now!", info.name);
    }

    return devAdapter;
}

DeviceAdapter::DeviceAdapter()
    : mCameraHandle(-1), mQueued(0), mDequeued(0)
{}

DeviceAdapter::~DeviceAdapter()
{
    // Close the camera handle and free the video info structure
    if(mCameraHandle)
    {
		close(mCameraHandle);
		mCameraHandle = -1;
    }

    if (mVideoInfo) {
        delete mVideoInfo;
        mVideoInfo = NULL;
    }
}

status_t DeviceAdapter::initialize(const CameraInfo& info)
{
    if (info.name == NULL) {
        FLOGE("invalid camera sensor name in initialize");
        return BAD_VALUE;
    }
    if (info.devPath == NULL) {
        FLOGE("invalid camera devpath in initialize");
        return BAD_VALUE;
    }

    mCameraHandle = open(info.devPath, O_RDWR);
    if (mCameraHandle < 0) {
		memset((void*)info.devPath, 0, sizeof(info.devPath));
		GetDevPath(info.name, (char*)info.devPath, CAMAERA_FILENAME_LENGTH);
		if (info.devPath[0] != '\0') {
			mCameraHandle = open(info.devPath, O_RDWR);
			if (mCameraHandle < 0) {
				FLOGE("can not open camera devpath:%s", info.devPath);
				return BAD_VALUE;
			}
		}
		else {
			FLOGE("can not open camera devpath:%s", info.devPath);
			return BAD_VALUE;
		}
    }

    mVideoInfo = new VideoInfo();
    if (mVideoInfo == NULL) {
        close(mCameraHandle);
        FLOGE("new VideoInfo failed");
        return NO_MEMORY;
    }

    int ret = NO_ERROR;
    ret = ioctl(mCameraHandle, VIDIOC_QUERYCAP, &mVideoInfo->cap);
    if (ret < 0) {
        close(mCameraHandle);
        delete mVideoInfo;
        FLOGE("query v4l2 capability failed");
        return BAD_VALUE;
    }
    if ((mVideoInfo->cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0)
    {
        close(mCameraHandle);
        delete mVideoInfo;
        FLOGE("v4l2 capability does not support capture");
        return BAD_VALUE;
    }

    // Initialize flags
    mPreviewing            = false;
    mVideoInfo->isStreamOn = false;
    mImageCapture          = false;

    return NO_ERROR;
}

int DeviceAdapter::getCaptureMode(int /*width*/,
                          int /*height*/)
{
    return 0;
}

status_t DeviceAdapter::setDeviceConfig(int         width,
                                        int         height,
                                        PixelFormat format,
                                        int         fps)
{
    if (mCameraHandle <= 0) {
        FLOGE("setDeviceConfig: DeviceAdapter uninitialized");
        return BAD_VALUE;
    }
    if ((width == 0) || (height == 0)) {
        FLOGE("setDeviceConfig: invalid parameters");
        return BAD_VALUE;
    }

    status_t ret = NO_ERROR;
	
#ifndef EVK_6SL //VIDIOC_S_INPUT not support in evk_6sl
    int input    = 1;
    ret = ioctl(mCameraHandle, VIDIOC_S_INPUT, &input);
    if (ret < 0) {
        FLOGE("Open: VIDIOC_S_INPUT Failed: %s", strerror(errno));
        return ret;
    }
#endif

    int vformat;
    vformat = convertPixelFormatToV4L2Format(format);

    if ((width > 1920) || (height > 1080)) {
        fps = 15;
    }
    FLOGI("Width * Height %d x %d format %d, fps: %d",
          width,
          height,
          vformat,
          fps);

    mVideoInfo->width       = width;
    mVideoInfo->height      = height;
    mVideoInfo->framesizeIn = (width * height << 1);
    mVideoInfo->formatIn    = vformat;

    mVideoInfo->param.type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE;
    mVideoInfo->param.parm.capture.timeperframe.numerator   = 1;
    mVideoInfo->param.parm.capture.timeperframe.denominator = fps;
    mVideoInfo->param.parm.capture.capturemode              = getCaptureMode(
        width,
        height);
    ret                                                     = ioctl(
        mCameraHandle,
        VIDIOC_S_PARM,
        &mVideoInfo->
        param);
    if (ret < 0) {
        FLOGE("Open: VIDIOC_S_PARM Failed: %s", strerror(errno));
        return ret;
    }

	memset(&mVideoInfo->format, 0, sizeof(mVideoInfo->format));
    mVideoInfo->format.type                 = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    mVideoInfo->format.fmt.pix.width        = width & 0xFFFFFFF8;
    mVideoInfo->format.fmt.pix.height       = height & 0xFFFFFFF8;
    mVideoInfo->format.fmt.pix.pixelformat  = vformat;
    mVideoInfo->format.fmt.pix.priv         = 0;
    mVideoInfo->format.fmt.pix.sizeimage    = 0;
    mVideoInfo->format.fmt.pix.bytesperline = 0;

    // Special stride alignment for YU12
    if (vformat == v4l2_fourcc('Y', 'U', '1', '2')){
        // Goolge define the the stride and c_stride for YUV420 format
        // y_size = stride * height
        // c_stride = ALIGN(stride/2, 16)
        // c_size = c_stride * height/2
        // size = y_size + c_size * 2
        // cr_offset = y_size
        // cb_offset = y_size + c_size
        // int stride = (width+15)/16*16;
        // int c_stride = (stride/2+16)/16*16;
        // y_size = stride * height
        // c_stride = ALIGN(stride/2, 16)
        // c_size = c_stride * height/2
        // size = y_size + c_size * 2
        // cr_offset = y_size
        // cb_offset = y_size + c_size

        // GPU and IPU take below stride calculation
        // GPU has the Y stride to be 32 alignment, and UV stride to be
        // 16 alignment.
        // IPU have the Y stride to be 2x of the UV stride alignment
        int stride = (width+31)/32*32;
        int c_stride = (stride/2+15)/16*16;
        mVideoInfo->format.fmt.pix.bytesperline = stride;
        mVideoInfo->format.fmt.pix.sizeimage    = stride*height+c_stride * height;
        FLOGI("Special handling for YV12 on Stride %d, size %d",
            mVideoInfo->format.fmt.pix.bytesperline,
            mVideoInfo->format.fmt.pix.sizeimage);
    }

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

status_t DeviceAdapter::registerCameraFrames(CameraFrame *pBuffer,
                                             int        & num)
{
    status_t ret = NO_ERROR;

    if ((pBuffer == NULL) || (num <= 0)) {
        FLOGE("requestCameraBuffers invalid pBuffer");
        return BAD_VALUE;
    }

    mVideoInfo->rb.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    mVideoInfo->rb.memory = V4L2_MEMORY_USERPTR;
    mVideoInfo->rb.count  = num;

    ret = ioctl(mCameraHandle, VIDIOC_REQBUFS, &mVideoInfo->rb);
    if (ret < 0) {
        FLOGE("VIDIOC_REQBUFS failed: %s", strerror(errno));
        return ret;
    }

    for (int i = 0; i < num; i++) {
        CameraFrame *buffer = pBuffer + i;
        memset(&mVideoInfo->buf, 0, sizeof(struct v4l2_buffer));
        mVideoInfo->buf.index    = i;
        mVideoInfo->buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        mVideoInfo->buf.memory   = V4L2_MEMORY_USERPTR;
        mVideoInfo->buf.m.offset = buffer->mPhyAddr;
        mVideoInfo->buf.length   = buffer->mSize;

        ret = ioctl(mCameraHandle, VIDIOC_QUERYBUF, &mVideoInfo->buf);
        if (ret < 0) {
            FLOGE("Unable to query buffer (%s)", strerror(errno));
            return ret;
        }

        // Associate each Camera buffer
        buffer->setObserver(this);
        mPreviewBufs.add((int)buffer, i);
    }

    mPreviewBufferSize  = pBuffer->mSize;
    mPreviewBufferCount = num;

    return ret;
}

status_t DeviceAdapter::fillCameraFrame(CameraFrame *frame)
{
    status_t ret = NO_ERROR;

    if (!mVideoInfo->isStreamOn) {
        return NO_ERROR;
    }

    int i = mPreviewBufs.valueFor((unsigned int)frame);
    if (i < 0) {
        return BAD_VALUE;
    }

    struct v4l2_buffer cfilledbuffer;
    memset(&cfilledbuffer, 0, sizeof (struct v4l2_buffer));
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cfilledbuffer.memory = V4L2_MEMORY_USERPTR;
    cfilledbuffer.index    = i;
    cfilledbuffer.m.offset = frame->mPhyAddr;

    ret = ioctl(mCameraHandle, VIDIOC_QBUF, &cfilledbuffer);
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

    for (int i = 0; i < queueableBufs; i++) {
        CameraFrame *frame = (CameraFrame *)mPreviewBufs.keyAt(i);
        mVideoInfo->buf.index    = i;
        mVideoInfo->buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        mVideoInfo->buf.memory   = V4L2_MEMORY_USERPTR;
        mVideoInfo->buf.m.offset = frame->mPhyAddr;

        ret = ioctl(mCameraHandle, VIDIOC_QBUF, &mVideoInfo->buf);
        if (ret < 0) {
            FLOGE("VIDIOC_QBUF Failed");
            return BAD_VALUE;
        }

        mQueued++;
    }

    enum v4l2_buf_type bufType;
    if (!mVideoInfo->isStreamOn) {
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

    if (mVideoInfo->isStreamOn) {
        bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        ret = ioctl(mCameraHandle, VIDIOC_STREAMOFF, &bufType);
        if (ret < 0) {
            FLOGE("StopStreaming: Unable to stop capture: %s", strerror(errno));
            return ret;
        }

        mVideoInfo->isStreamOn = false;
    }

    mQueued   = 0;
    mDequeued = 0;
    mPreviewBufs.clear();

    return ret;
}

status_t DeviceAdapter::startPreview()
{
    status_t ret = NO_ERROR;

    if (mPreviewing) {
        FLOGE("DeviceAdapter: startPreview but preview running");
        return BAD_VALUE;
    }

    Mutex::Autolock lock(mPreviewBufsLock);
    ret = startDeviceLocked();

    if(ret == 0) {
        mPreviewing = true;
    }

    return ret;
}

status_t DeviceAdapter::stopPreview()
{
    int ret = NO_ERROR;

    if (!mPreviewing) {
        FLOGE("DeviceAdapter: stopPreview but preview not running");
        return NO_INIT;
    }

    Mutex::Autolock lock(mPreviewBufsLock);
    mPreviewing = false;
    ret         = stopDeviceLocked();

    return ret;
}

status_t DeviceAdapter::startImageCapture()
{
    status_t ret = NO_ERROR;

    if (mImageCapture) {
        FLOGE("DeviceAdapter: startPreview but preview running");
        return BAD_VALUE;
    }

    Mutex::Autolock lock(mPreviewBufsLock);
    mImageCapture = true;
    ret           = startDeviceLocked();

    return ret;
}

status_t DeviceAdapter::stopImageCapture()
{
    int ret = NO_ERROR;

    if (!mImageCapture) {
        FLOGE("DeviceAdapter: stopPreview but preview not running");
        return NO_INIT;
    }

    Mutex::Autolock lock(mPreviewBufsLock);
    mImageCapture = false;
    ret           = stopDeviceLocked();

    return ret;
}

CameraFrame * DeviceAdapter::acquireCameraFrame()
{
    int ret;

    struct v4l2_buffer cfilledbuffer;
    memset(&cfilledbuffer, 0, sizeof (cfilledbuffer));
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cfilledbuffer.memory = V4L2_MEMORY_USERPTR;

    /* DQ */
    ret = ioctl(mCameraHandle, VIDIOC_DQBUF, &cfilledbuffer);
    if (ret < 0) {
        FLOGE("GetFrame: VIDIOC_DQBUF Failed");
        return NULL;
    }
    mDequeued++;

    int index = cfilledbuffer.index;
    FSL_ASSERT(!mPreviewBufs.isEmpty(), "mPreviewBufs is empty");
    return (CameraFrame *)mPreviewBufs.keyAt(index);
}

// #define FSL_CAMERAHAL_DUMP
static void bufferDump(CameraFrame * /*frame*/)
{
#ifdef FSL_CAMERAHAL_DUMP

    // for test code
    char value[100];
    memset(value, 0, sizeof(value));
    static int vflg = 0;
    property_get("rw.camera.test", value, "");
    if (strcmp(value, "1") == 0)
        vflg = 1;
    if (vflg) {
        FILE *pf = NULL;
        pf = fopen("/sdcard/camera_tst.data", "wb");
        if (pf == NULL) {
            FLOGI("open /sdcard/camera_tst.data failed");
        }
        else {
            FLOGI("-----write-----");
            fwrite(frame->mVirtAddr, frame->mSize, 1, pf);
            fclose(pf);
        }
        vflg = 0;
    }
#endif // ifdef FSL_CAMERAHAL_DUMP
}

int DeviceAdapter::deviceThread()
{
    CameraFrame *frame = NULL;

    frame = acquireCameraFrame();
    if (!frame) {
        if (mQueued - mDequeued <= 0) {
            // if stop preview, then exit.
            if (!mPreviewing) {
                FLOGI("preview stop, so exit device thread");
                return BAD_VALUE;
            }
            else {
                // to check buffer in another cycle.
                FLOGI("no buffer in v4l driver, check it next time");
                return NO_ERROR;
            }
        }
        FLOGW("device, frame = null, %d buffers still in v4l, q %d, dq %d",
              mQueued - mDequeued, mQueued, mDequeued);

		//it's normal that mQueued > mDequeued, no need to sent err.
		/*
        if (mErrorListener != NULL) {
            mErrorListener->handleError(ERROR_FATAL);
        }*/
        return NO_ERROR;
    }

    if (mImageCapture) {
        sp<CameraEvent> cameraEvt = new CameraEvent();
        cameraEvt->mEventType = CameraEvent::EVENT_SHUTTER;
        dispatchEvent(cameraEvt);

        frame->mFrameType = CameraFrame::IMAGE_FRAME;
    }
    else {
        frame->mFrameType = CameraFrame::PREVIEW_FRAME;
    }

    dispatchCameraFrame(frame);
    if (mImageCapture || !mPreviewing) {
        FLOGI("device thread exit after take picture");
        return ALREADY_EXISTS;
    }

    return NO_ERROR;
}

status_t DeviceAdapter::autoFocus()
{
    if (mAutoFocusThread != NULL) {
        mAutoFocusThread.clear();
    }

    mAutoFocusThread = new AutoFocusThread(this);
    if (mAutoFocusThread == NULL) {
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

    // exit the thread.
    return UNKNOWN_ERROR;
}

void DeviceAdapter::handleFrameRelease(CameraFrame *buffer)
{
    if (mPreviewing) {
        fillCameraFrame(buffer);
    }
}

void DeviceAdapter::setErrorListener(CameraErrorListener *listener)
{
    mErrorListener = listener;
}

void DeviceAdapter::setCameraBufferProvide(CameraBufferProvider *bufferProvider)
{
    if (bufferProvider != NULL) {
        bufferProvider->addBufferListener(this);
    }
    mBufferProvider = bufferProvider;
}

void DeviceAdapter::onBufferCreat(CameraFrame *pBuffer,
                                  int          num)
{
    registerCameraFrames(pBuffer, num);
}

void DeviceAdapter::onBufferDestroy()
{
    mPreviewBufs.clear();
}

