/*
 * Copyright (C) 2012-2015 Freescale Semiconductor, Inc.
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

#include "CameraUtil.h"
#include "UvcDevice.h"

UvcDevice::UvcDevice()
{
    mbOmitFirstFrame = false;
}

UvcDevice::~UvcDevice()
{
}

void UvcDevice::adjustSensorFormats(int *src, int len)
{
    if (src == NULL || len == 0) {
        return;
    }

    mDefaultFormat = 0;
    memset(mSensorFormats, 0, sizeof(mSensorFormats));
    int k = 0;
    for (int i=0; i<len && i<MAX_SENSOR_FORMAT && k<MAX_SENSOR_FORMAT; i++) {
        switch (src[i]) {
            case v4l2_fourcc('N', 'V', '1', '2'):
                mSensorFormats[k++] = HAL_PIXEL_FORMAT_YCbCr_420_SP;
                mDefaultFormat = HAL_PIXEL_FORMAT_YCbCr_420_SP;
                break;

            case v4l2_fourcc('Y', 'V', '1', '2'):
                mSensorFormats[k++] = HAL_PIXEL_FORMAT_YCbCr_420_P;
                break;

            case v4l2_fourcc('Y', 'U', 'Y', 'V'):
                if (mDefaultFormat == 0) {
                    mDefaultFormat = HAL_PIXEL_FORMAT_YCbCr_422_I;
                }
                break;

            case v4l2_fourcc('B', 'L', 'O', 'B'):
                mSensorFormats[k++] = HAL_PIXEL_FORMAT_BLOB;
                break;

            case v4l2_fourcc('R', 'A', 'W', 'S'):
                mSensorFormats[k++] = HAL_PIXEL_FORMAT_RAW16;
                break;

            default:
                FLOGE("Error: format 0x%x not supported!", src[i]);
                break;
        }
    }

    return;
}

status_t UvcDevice::setDeviceConfig(int         width,
                                    int         height,
                                    PixelFormat /*format*/,
                                    int         fps)
{
    if (mCameraHandle <= 0) {
        if (pDevPath != NULL) {
            mCameraHandle = open(pDevPath, O_RDWR);
        }
        if (mCameraHandle <= 0) {
            FLOGE("setDeviceConfig: DeviceAdapter uninitialized");
            return BAD_VALUE;
        }
    }
    if ((width == 0) || (height == 0)) {
        FLOGE("setDeviceConfig: invalid parameters");
        return BAD_VALUE;
    }

    status_t ret = NO_ERROR;
    int vformat;
    vformat = convertPixelFormatToV4L2Format(mDefaultFormat);

    if ((width > 1920) || (height > 1080)) {
        fps = 15;
    }
    FLOGI("Width * Height %d x %d format 0x%x, fps: %d",
          width, height, vformat, fps);

    mVideoInfo->width       = width;
    mVideoInfo->height      = height;
    mVideoInfo->framesizeIn = (width * height << 1);
    mVideoInfo->formatIn    = vformat;

    struct v4l2_streamparm param;
    memset(&param, 0, sizeof(param));
    param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    param.parm.capture.timeperframe.numerator   = 1;
    param.parm.capture.timeperframe.denominator = fps;
    ret = ioctl(mCameraHandle, VIDIOC_S_PARM, &param);
    if (ret < 0) {
        FLOGE("Open: VIDIOC_S_PARM Failed: %s", strerror(errno));
        return ret;
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type                 = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width        = width & 0xFFFFFFF8;
    fmt.fmt.pix.height       = height & 0xFFFFFFF8;
    fmt.fmt.pix.pixelformat  = vformat;
    fmt.fmt.pix.priv         = 0;
    fmt.fmt.pix.sizeimage    = 0;
    fmt.fmt.pix.bytesperline = 0;

    ret = ioctl(mCameraHandle, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        FLOGE("Open: VIDIOC_S_FMT Failed: %s", strerror(errno));
        return ret;
    }

    return ret;
}

void UvcDevice::setPreviewPixelFormat()
{
    mPreviewNeedCsc = true;
    if (mDefaultFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP) {
        mPreviewNeedCsc = false;
    }

    int n = 0;
    if (mPreviewNeedCsc) {
        mAvailableFormats[n++] = HAL_PIXEL_FORMAT_YCbCr_420_SP;
    }
    for (int i=0; i < MAX_SENSOR_FORMAT && (mSensorFormats[i] != 0) &&
                  n < MAX_SENSOR_FORMAT; i++) {
        mAvailableFormats[n++] = mSensorFormats[i];
    }
    mAvailableFormatCount = n;
    mPreviewPixelFormat = HAL_PIXEL_FORMAT_YCbCr_420_SP;
}

void UvcDevice::setPicturePixelFormat()
{
    mPictureNeedCsc = true;
    if (mDefaultFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP) {
        mPictureNeedCsc = false;
    }

    mPicturePixelFormat = HAL_PIXEL_FORMAT_YCbCr_420_SP;
}

#define LOGI_C920 "HD Pro Webcam C920"

status_t UvcDevice::initSensorInfo(const CameraInfo& info)
{
    struct v4l2_capability v4l2_cap;

    if (mCameraHandle < 0) {
        FLOGE("OvDevice: initParameters sensor has not been opened");
        return BAD_VALUE;
    }
    pDevPath = info.devPath;

    int retVal = 0;
    retVal = ioctl(mCameraHandle, VIDIOC_QUERYCAP, &v4l2_cap);
    if((retVal == 0) && strstr((const char*)v4l2_cap.card, LOGI_C920)) {
        mbOmitFirstFrame = true;
    }

    // first read sensor format.
    int ret = 0, index = 0;
    int sensorFormats[MAX_SENSOR_FORMAT];
    memset(mAvailableFormats, 0, sizeof(mAvailableFormats));
    memset(sensorFormats, 0, sizeof(sensorFormats));
    memset(mPreviewResolutions, 0, sizeof(mPreviewResolutions));
    memset(mPictureResolutions, 0, sizeof(mPictureResolutions));

    struct v4l2_fmtdesc vid_fmtdesc;
    while (ret == 0) {
        vid_fmtdesc.index = index;
        vid_fmtdesc.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ret               = ioctl(mCameraHandle, VIDIOC_ENUM_FMT, &vid_fmtdesc);
        FLOG_RUNTIME("index:%d,ret:%d, format:%c%c%c%c", index, ret,
                     vid_fmtdesc.pixelformat & 0xFF,
                     (vid_fmtdesc.pixelformat >> 8) & 0xFF,
                     (vid_fmtdesc.pixelformat >> 16) & 0xFF,
                     (vid_fmtdesc.pixelformat >> 24) & 0xFF);
        if (ret == 0) {
            sensorFormats[index++] = vid_fmtdesc.pixelformat;
        }
    }
    sensorFormats[index++] = v4l2_fourcc('Y', 'V', '1', '2');
    sensorFormats[index++] = v4l2_fourcc('B', 'L', 'O', 'B');
    sensorFormats[index++] = v4l2_fourcc('R', 'A', 'W', 'S');

    //mAvailableFormatCount = index;
    adjustSensorFormats(sensorFormats, index);
    if (mDefaultFormat == 0) {
        FLOGE("Error: invalid mDefaultFormat:%d", mDefaultFormat);
        return BAD_VALUE;
    }

    ret = 0;
    index = 0;
    char TmpStr[20];
    int  previewCnt = 0, pictureCnt = 0;
    struct v4l2_frmsizeenum vid_frmsize;
    struct v4l2_frmivalenum vid_frmval;
    while (ret == 0) {
        memset(TmpStr, 0, 20);
        memset(&vid_frmsize, 0, sizeof(struct v4l2_frmsizeenum));
        vid_frmsize.index        = index++;
        vid_frmsize.pixel_format = 
                    convertPixelFormatToV4L2Format(mDefaultFormat);
        ret = ioctl(mCameraHandle,
                    VIDIOC_ENUM_FRAMESIZES, &vid_frmsize);

        if (ret == 0) {
            //uvc need do csc, so omit large resolution.
            if (vid_frmsize.discrete.width > 1920 ||
                     vid_frmsize.discrete.height > 1080) {
                continue;
            }

            FLOGI("enum frame size w:%d, h:%d",
                       vid_frmsize.discrete.width, vid_frmsize.discrete.height);
            memset(&vid_frmval, 0, sizeof(struct v4l2_frmivalenum));
            vid_frmval.index        = 0;
            vid_frmval.pixel_format = vid_frmsize.pixel_format;
            vid_frmval.width        = vid_frmsize.discrete.width;
            vid_frmval.height       = vid_frmsize.discrete.height;

            ret = ioctl(mCameraHandle, VIDIOC_ENUM_FRAMEINTERVALS, &vid_frmval);
            if (ret == 0) {
                FLOGI("vid_frmval denominator:%d, numeraton:%d",
                             vid_frmval.discrete.denominator,
                             vid_frmval.discrete.numerator);
                mPictureResolutions[pictureCnt++] = vid_frmsize.discrete.width;
                mPictureResolutions[pictureCnt++] = vid_frmsize.discrete.height;

                if (vid_frmval.discrete.denominator /
                    vid_frmval.discrete.numerator > 15) {
                    mPreviewResolutions[previewCnt++] = vid_frmsize.discrete.width;
                    mPreviewResolutions[previewCnt++] = vid_frmsize.discrete.height;;
                }
            }
        }
    } // end while

    mPreviewResolutionCount = previewCnt;
    mPictureResolutionCount = pictureCnt;

    mMinFrameDuration = 33331760L;
    mMaxFrameDuration = 30000000000L;
    int i;
    for (i=0; i<MAX_RESOLUTION_SIZE && i<pictureCnt; i+=2) {
        FLOGI("SupportedPictureSizes: %d x %d", mPictureResolutions[i], mPictureResolutions[i+1]);
    }

    adjustPreviewResolutions();
    for (i=0; i<MAX_RESOLUTION_SIZE && i<previewCnt; i+=2) {
        FLOGI("SupportedPreviewSizes: %d x %d", mPreviewResolutions[i], mPreviewResolutions[i+1]);
    }
    FLOGI("FrameDuration is %lld, %lld", mMinFrameDuration, mMaxFrameDuration);

    i = 0;
    mTargetFpsRange[i++] = 10;
    mTargetFpsRange[i++] = 15;
    mTargetFpsRange[i++] = 25;
    mTargetFpsRange[i++] = 30;

    setMaxPictureResolutions();
    FLOGI("mMaxWidth:%d, mMaxHeight:%d", mMaxWidth, mMaxHeight);
    mFocalLength = 3.42f;
    mPhysicalWidth = 3.673f;
    mPhysicalHeight = 2.738f;

    return NO_ERROR;
}

status_t UvcDevice::registerCameraBuffers(CameraFrame *pBuffer, int &num)
{
    struct v4l2_buffer buf;
    struct v4l2_requestbuffers req;

    if ((mCameraHandle <= 0) || (pBuffer == NULL) || (num <= 0)) {
        FLOGE("requestCameraBuffers invalid pBuffer");
        return BAD_VALUE;
    }

    memset(&req, 0, sizeof (req));
    req.count = num;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(mCameraHandle, VIDIOC_REQBUFS, &req) < 0) {
        FLOGE("v4l_capture_setup: VIDIOC_REQBUFS failed\n");
        return BAD_VALUE;
    }

    memset(mUvcBuffers, 0, sizeof(mUvcBuffers));
    for (int i = 0; i < num; i++) {
        CameraFrame *buffer = pBuffer + i;
        // Associate each Camera buffer
        buffer->setObserver(this);
        mDeviceBufs[i] = buffer;

        memset(&buf, 0, sizeof (buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.index = i;
        if (ioctl(mCameraHandle, VIDIOC_QUERYBUF, &buf) < 0) {
            FLOGE("VIDIOC_QUERYBUF error\n");
            return BAD_VALUE;
        }

        mUvcBuffers[i].mSize = buf.length;
        mUvcBuffers[i].mPhyAddr = (size_t)buf.m.offset;
        mUvcBuffers[i].mVirtAddr = (void *)mmap(NULL, mUvcBuffers[i].mSize,
                    PROT_READ | PROT_WRITE, MAP_SHARED, mCameraHandle,
                    mUvcBuffers[i].mPhyAddr);
        mUvcBuffers[i].mFormat = mDefaultFormat;
        mUvcBuffers[i].mWidth = buffer->mWidth;
        mUvcBuffers[i].mHeight = buffer->mHeight;
        memset(mUvcBuffers[i].mVirtAddr, 0xFF, mUvcBuffers[i].mSize);
    }

    mBufferSize  = pBuffer->mSize;
    mBufferCount = num;

    return NO_ERROR;
}

status_t UvcDevice::fillCameraFrame(CameraFrame *frame)
{
    status_t ret = NO_ERROR;

    if (!mVideoInfo->isStreamOn) {
        return NO_ERROR;
    }

    int i = frame->mIndex;
    if (i < 0) {
        return BAD_VALUE;
    }

    struct v4l2_buffer cfilledbuffer;
    memset(&cfilledbuffer, 0, sizeof (struct v4l2_buffer));
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cfilledbuffer.memory = V4L2_MEMORY_MMAP;
    cfilledbuffer.index    = i;
    cfilledbuffer.m.offset = mUvcBuffers[i].mPhyAddr;

    ret = ioctl(mCameraHandle, VIDIOC_QBUF, &cfilledbuffer);
    if (ret < 0) {
        FLOGE("fillCameraFrame: VIDIOC_QBUF Failed");
        return BAD_VALUE;
    }
    mQueued++;

    return ret;
}

CameraFrame * UvcDevice::acquireCameraFrame()
{
    int n;
    fd_set rfds;
    struct timeval tv;
    struct v4l2_buffer cfilledbuffer;
    CameraFrame *camBuf = NULL;
    int capCount = 0;

cap:
    capCount++;
    FD_ZERO(&rfds);
    FD_SET(mCameraHandle, &rfds);
    tv.tv_sec = MAX_DEQUEUE_WAIT_TIME;
    tv.tv_usec = 0;
    n = select(mCameraHandle+1, &rfds, NULL, NULL, &tv);
    if(n < 0) {
        FLOGE("Error!Query the V4L2 Handler state error.");
    }
    else if(n == 0) {
        FLOGI("Warning!Time out wait for V4L2 capture reading operation!");
    }
    else if(FD_ISSET(mCameraHandle, &rfds)) {
        memset(&cfilledbuffer, 0, sizeof (cfilledbuffer));
        cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cfilledbuffer.memory = V4L2_MEMORY_MMAP;
        int rtval;
        rtval = ioctl(mCameraHandle, VIDIOC_DQBUF, &cfilledbuffer);
        if (rtval < 0) {
            FLOGE("Camera VIDIOC_DQBUF failure, ret=%d", rtval);
            return camBuf;
        }
        mQueued --;

        //for logi C920, when shift from 800x600 to 640x480, the first frame is damaged.
        if(mImageCapture && mbOmitFirstFrame &&
            (mVideoInfo->width == 640) && (mVideoInfo->height == 480)) {
            ioctl(mCameraHandle, VIDIOC_QBUF, &cfilledbuffer);
            mQueued++;
            if(capCount < 2) {
                ALOGI("acquireCameraFrame, omit first frame");
                goto cap;
            }
        }

        int index = cfilledbuffer.index;
        fAssert(index >= 0 && index < mBufferCount);
        camBuf = mDeviceBufs[index];
        camBuf->mTimeStamp = systemTime();

        //should do hardware accelerate.
        if(mPreviewNeedCsc || mPictureNeedCsc) {
            doColorConvert(camBuf, &mUvcBuffers[index]);
        }
        else
            memcpy(camBuf->mVirtAddr,
                   mUvcBuffers[index].mVirtAddr, camBuf->mSize);
    }
    else {
        FLOGE("Error!Query the V4L2 Handler state, no known error.");
    }

    return camBuf;
}

status_t UvcDevice::startDeviceLocked()
{
    status_t ret = NO_ERROR;
    struct v4l2_buffer cfilledbuffer;

    fAssert(mBufferProvider != NULL);

    int state;
    for (int i = 0; i < mBufferCount; i++) {
        CameraFrame* frame = mDeviceBufs[i];
        state = frame->getState();
        if (state != CameraFrame::BUFS_FREE) {
            continue;
        }
        frame->setState(CameraFrame::BUFS_IN_DRIVER);

        memset(&cfilledbuffer, 0, sizeof (struct v4l2_buffer));
        cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cfilledbuffer.memory = V4L2_MEMORY_MMAP;
        cfilledbuffer.index    = i;
        cfilledbuffer.m.offset = mUvcBuffers[i].mPhyAddr;
        ret = ioctl(mCameraHandle, VIDIOC_QBUF, &cfilledbuffer);
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

status_t UvcDevice::stopDeviceLocked()
{
    int ret = 0;
    ret = DeviceAdapter::stopDeviceLocked();
    if (ret != 0) {
        FLOGE("call %s failed", __FUNCTION__);
        return ret;
    }

    for (int i = 0; i < mBufferCount; i++) {
        if (mUvcBuffers[i].mVirtAddr != NULL && mUvcBuffers[i].mSize > 0) {
            munmap(mUvcBuffers[i].mVirtAddr, mUvcBuffers[i].mSize);
        }
    }

    if (mCameraHandle > 0) {
        close(mCameraHandle);
        mCameraHandle = -1;
    }
    return ret;
}

status_t UvcDevice::adjustPreviewResolutions()
{
    int xTmp, yTmp, xMax, yMax, idx;
    idx = 0;
    xTmp = xMax = mPreviewResolutions[0];
    yTmp = yMax = mPreviewResolutions[1];
    for (int i=0; i<MAX_RESOLUTION_SIZE; i+=2) {
        if (mPreviewResolutions[i] > xMax) {
            xMax = mPreviewResolutions[i];
            yMax = mPreviewResolutions[i+1];
            idx = i;
        }
    }

    mPreviewResolutions[0] = xMax;
    mPreviewResolutions[1] = yMax;
    mPreviewResolutions[idx] = xTmp;
    mPreviewResolutions[idx+1] = yTmp;

    return 0;
}

status_t UvcDevice::setMaxPictureResolutions()
{
    int xMax, yMax;
    xMax = mPictureResolutions[0];
    yMax = mPictureResolutions[1];

    for (int i=0; i<MAX_RESOLUTION_SIZE; i+=2) {
        if (mPictureResolutions[i] > xMax || mPictureResolutions[i+1] > yMax) {
            xMax = mPictureResolutions[i];
            yMax = mPictureResolutions[i+1];
        }
    }

    mMaxWidth = xMax;
    mMaxHeight = yMax;

    return 0;
}

void UvcDevice::doColorConvert(StreamBuffer *dst, StreamBuffer *src)
{
    if (dst->mFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP &&
        src->mFormat == HAL_PIXEL_FORMAT_YCbCr_422_I) {
        convertYUYUToNV12(dst, src);
        return;
    }

    FLOGE("%s not support format:0x%x converted to format:0x%x",
               __FUNCTION__, src->mFormat, dst->mFormat);
}

void UvcDevice::convertYUYUToNV12(StreamBuffer *dst, StreamBuffer *src)
{
    unsigned char *pSrcBufs = (unsigned char*)src->mVirtAddr;
    unsigned char *pDstBufs = (unsigned char*)dst->mVirtAddr;
    unsigned int bufWidth = src->mWidth;
    unsigned int bufHeight = src->mHeight;

    unsigned char *pSrcY1Offset = pSrcBufs;
    unsigned char *pSrcY2Offset = pSrcBufs + (bufWidth << 1);
    unsigned char *pSrcY3Offset = pSrcBufs + (bufWidth << 1) * 2;
    unsigned char *pSrcY4Offset = pSrcBufs + (bufWidth << 1) * 3;
    unsigned char *pSrcU1Offset = pSrcY1Offset + 1;
    unsigned char *pSrcU2Offset = pSrcY2Offset + 1;
    unsigned char *pSrcU3Offset = pSrcY3Offset + 1;
    unsigned char *pSrcU4Offset = pSrcY4Offset + 1;
    unsigned char *pSrcV1Offset = pSrcY1Offset + 3;
    unsigned char *pSrcV2Offset = pSrcY2Offset + 3;
    unsigned char *pSrcV3Offset = pSrcY3Offset + 3;
    unsigned char *pSrcV4Offset = pSrcY4Offset + 3;
    unsigned int srcYStride = (bufWidth << 1) * 3;
    unsigned int srcUVStride = srcYStride;

    unsigned char *pDstY1Offset = pDstBufs;
    unsigned char *pDstY2Offset = pDstBufs + bufWidth;
    unsigned char *pDstY3Offset = pDstBufs + bufWidth * 2;
    unsigned char *pDstY4Offset = pDstBufs + bufWidth * 3;
    unsigned char *pDstU1Offset = pDstBufs + bufWidth * bufHeight;
    unsigned char *pDstU2Offset = pDstBufs + bufWidth * (bufHeight + 1);
    unsigned char *pDstV1Offset = pDstU1Offset + 1;
    unsigned char *pDstV2Offset = pDstU2Offset + 1;
    unsigned int dstYStride = bufWidth * 3;
    unsigned int dstUVStride = bufWidth;

    unsigned int nw, nh;
    for(nh = 0; nh < (bufHeight >> 2); nh++) {
        for(nw=0; nw < (bufWidth >> 1); nw++) {
            *pDstY1Offset++ = *pSrcY1Offset;
            *pDstY2Offset++ = *pSrcY2Offset;
            *pDstY3Offset++ = *pSrcY3Offset;
            *pDstY4Offset++ = *pSrcY4Offset;

            pSrcY1Offset += 2;
            pSrcY2Offset += 2;
            pSrcY3Offset += 2;
            pSrcY4Offset += 2;

            *pDstY1Offset++ = *pSrcY1Offset;
            *pDstY2Offset++ = *pSrcY2Offset;
            *pDstY3Offset++ = *pSrcY3Offset;
            *pDstY4Offset++ = *pSrcY4Offset;

            pSrcY1Offset += 2;
            pSrcY2Offset += 2;
            pSrcY3Offset += 2;
            pSrcY4Offset += 2;

            *pDstU1Offset = *pSrcU1Offset;
            *pDstU2Offset = *pSrcU3Offset;
            pDstU1Offset += 2;
            pDstU2Offset += 2;
            pSrcU1Offset += 4;
            pSrcU3Offset += 4;

            *pDstV1Offset = *pSrcV1Offset;
            *pDstV2Offset = *pSrcV3Offset;
            pDstV1Offset += 2;
            pDstV2Offset += 2;
            pSrcV1Offset += 4;
            pSrcV3Offset += 4;
        }
        pSrcY1Offset += srcYStride;
        pSrcY2Offset += srcYStride;
        pSrcY3Offset += srcYStride;
        pSrcY4Offset += srcYStride;

        pSrcU1Offset += srcUVStride;
        pSrcU3Offset += srcUVStride;
        pSrcV1Offset += srcUVStride;
        pSrcV3Offset += srcUVStride;

        pDstY1Offset += dstYStride;
        pDstY2Offset += dstYStride;
        pDstY3Offset += dstYStride;
        pDstY4Offset += dstYStride;

        pDstU1Offset += dstUVStride;
        pDstU2Offset += dstUVStride;
        pDstV1Offset += dstUVStride;
        pDstV2Offset += dstUVStride;
    }
}

