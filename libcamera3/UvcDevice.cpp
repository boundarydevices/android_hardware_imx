/*
 * Copyright (C) 2012-2016 Freescale Semiconductor, Inc.
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

#include "UvcDevice.h"
#include "DMAStream.h"

#define LOGI_C920 "HD Pro Webcam C920"

//----------------------UvcDevice--------------------
Camera* UvcDevice::newInstance(int32_t id, char* name, int32_t facing,
                               int32_t orientation, char* path)
{
    ALOGI("%s usb sensor name:%s", __func__, name);
    UvcDevice* device = NULL;
    if (strstr(name, LOGI_C920)) {
        ALOGI("%s create LogiC920 device", __func__);
        device = new LogiC920(id, facing, orientation, path);
    }
    else {
        ALOGI("%s usb sensor:%s use standard UVC device", __func__, name);
        device = new UvcDevice(id, facing, orientation, path);
    }

    return device;
}

UvcDevice::UvcDevice(int32_t id, int32_t facing, int32_t orientation,
                     char* path, bool createStream)
    : Camera(id, facing, orientation, path)
{
    if (createStream) {
        mVideoStream = new UvcStream(this, path);
    }
}

UvcDevice::~UvcDevice()
{
}

status_t UvcDevice::initSensorStaticData()
{
    int32_t fd = open(mDevPath, O_RDWR);
    if (fd < 0) {
        ALOGE("%s open path:%s failed", __func__, mDevPath);
        return BAD_VALUE;
    }

    // first read sensor format.
    int ret = 0, index = 0;
    int sensorFormats[MAX_SENSOR_FORMAT];
    int availFormats[MAX_SENSOR_FORMAT];
    memset(sensorFormats, 0, sizeof(sensorFormats));
    memset(availFormats, 0, sizeof(availFormats));

    struct v4l2_fmtdesc vid_fmtdesc;
    while (ret == 0) {
        vid_fmtdesc.index = index;
        vid_fmtdesc.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ret = ioctl(fd, VIDIOC_ENUM_FMT, &vid_fmtdesc);
        ALOGV("index:%d,ret:%d, format:%c%c%c%c", index, ret,
                     vid_fmtdesc.pixelformat & 0xFF,
                     (vid_fmtdesc.pixelformat >> 8) & 0xFF,
                     (vid_fmtdesc.pixelformat >> 16) & 0xFF,
                     (vid_fmtdesc.pixelformat >> 24) & 0xFF);
        if (ret == 0) {
            sensorFormats[index] = vid_fmtdesc.pixelformat;
            availFormats[index++] = vid_fmtdesc.pixelformat;
        }
    }

    mSensorFormatCount = changeSensorFormats(sensorFormats, mSensorFormats, index);
    if (mSensorFormatCount == 0) {
        ALOGE("%s no sensor format enum", __func__);
        close(fd);
        return BAD_VALUE;
    }

    availFormats[index++] = v4l2_fourcc('N', 'V', '1', '2');
    availFormats[index++] = v4l2_fourcc('Y', 'V', '1', '2');
    availFormats[index++] = v4l2_fourcc('N', 'V', '2', '1');
    availFormats[index++] = v4l2_fourcc('B', 'L', 'O', 'B');
    availFormats[index++] = v4l2_fourcc('R', 'A', 'W', 'S');
    mAvailableFormatCount = changeSensorFormats(availFormats, mAvailableFormats, index);

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
        vid_frmsize.pixel_format = convertPixelFormatToV4L2Format(mSensorFormats[0]);
        ret = ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &vid_frmsize);
        if (ret != 0) {
            continue;
        }

        ALOGI("enum frame size w:%d, h:%d",
                vid_frmsize.discrete.width, vid_frmsize.discrete.height);
        memset(&vid_frmval, 0, sizeof(struct v4l2_frmivalenum));
        vid_frmval.index        = 0;
        vid_frmval.pixel_format = vid_frmsize.pixel_format;
        vid_frmval.width        = vid_frmsize.discrete.width;
        vid_frmval.height       = vid_frmsize.discrete.height;

        ret = ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &vid_frmval);
        if (ret != 0) {
            continue;
        }
        ALOGI("vid_frmval denominator:%d, numeraton:%d",
                vid_frmval.discrete.denominator,
                vid_frmval.discrete.numerator);
        if (vid_frmval.discrete.denominator /
                vid_frmval.discrete.numerator >= 5) {
            mPictureResolutions[pictureCnt++] = vid_frmsize.discrete.width;
            mPictureResolutions[pictureCnt++] = vid_frmsize.discrete.height;
        }

        if (vid_frmval.discrete.denominator /
                vid_frmval.discrete.numerator > 15) {
            mPreviewResolutions[previewCnt++] = vid_frmsize.discrete.width;
            mPreviewResolutions[previewCnt++] = vid_frmsize.discrete.height;
        }
    } // end while

    mPreviewResolutionCount = previewCnt;
    mPictureResolutionCount = pictureCnt;

    mMinFrameDuration = 33331760L;
    mMaxFrameDuration = 30000000000L;
    int i;
    for (i=0; i<MAX_RESOLUTION_SIZE && i<pictureCnt; i+=2) {
        ALOGI("SupportedPictureSizes: %d x %d", mPictureResolutions[i], mPictureResolutions[i+1]);
    }

    adjustPreviewResolutions();
    for (i=0; i<MAX_RESOLUTION_SIZE && i<previewCnt; i+=2) {
        ALOGI("SupportedPreviewSizes: %d x %d", mPreviewResolutions[i], mPreviewResolutions[i+1]);
    }
    ALOGI("FrameDuration is %lld, %lld", mMinFrameDuration, mMaxFrameDuration);

    i = 0;
    mTargetFpsRange[i++] = 10;
    mTargetFpsRange[i++] = 30;
    mTargetFpsRange[i++] = 30;
    mTargetFpsRange[i++] = 30;

    setMaxPictureResolutions();
    ALOGI("mMaxWidth:%d, mMaxHeight:%d", mMaxWidth, mMaxHeight);
    mFocalLength = 3.42f;
    mPhysicalWidth = 3.673f;
    mPhysicalHeight = 2.738f;
    mActiveArrayWidth = 1920;
    mActiveArrayHeight = 1080;
    mPixelArrayWidth = 1920;
    mPixelArrayHeight = 1080;

    ALOGI("UvcDevice, mFocalLength:%f, mPhysicalWidth:%f, mPhysicalHeight %f",
        mFocalLength, mPhysicalWidth, mPhysicalHeight);

    close(fd);
    return 0;
}

int32_t UvcDevice::UvcStream::onDeviceConfigureLocked()
{
    ALOGI("%s", __func__);

    int32_t ret = 0;
    if (mDev <= 0) {
        // usb camera should open dev node again.
        // because when stream off, the dev node must close.
        mDev = open(mUvcPath, O_RDWR);
        if (mDev <= 0) {
            ALOGE("%s invalid fd handle", __func__);
            return BAD_VALUE;
        }
    }

    return DMAStream::onDeviceConfigureLocked();
}

int32_t UvcDevice::UvcStream::onDeviceStopLocked()
{
    ALOGI("%s", __func__);
    int32_t ret = DMAStream::onDeviceStopLocked();
    // usb camera must close device after stream off.
    if (mDev > 0) {
        close(mDev);
        mDev = -1;
    }

    return ret;
}

int32_t UvcDevice::UvcStream::onDeviceStartLocked()
{
    ALOGI("%s", __func__);
    return DMAStream::onDeviceStartLocked();
}

int32_t UvcDevice::UvcStream::onFrameAcquireLocked()
{
    ALOGV("%s", __func__);
    return DMAStream::onFrameAcquireLocked();
}

int32_t UvcDevice::UvcStream::onFrameReturnLocked(int32_t index, StreamBuffer& buf)
{
    ALOGV("%s", __func__);
    return DMAStream::onFrameReturnLocked(index, buf);
}

// usb camera require the specific buffer size.
int32_t UvcDevice::UvcStream::getDeviceBufferSize()
{
    int32_t size = 0;
    switch (mFormat) {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            size = ((mWidth + 16) & (~15)) * mHeight * 3 / 2;
            break;

         case HAL_PIXEL_FORMAT_YCbCr_420_P: {
            int32_t stride = (mWidth+31)/32*32;
            int32_t c_stride = (stride/2+15)/16*16;
            size = (stride + c_stride) * mHeight;
             break;
         }

         case HAL_PIXEL_FORMAT_YCbCr_422_I:
            size = mWidth * mHeight * 2;
             break;

        default:
            ALOGE("Error: %s format not supported", __func__);
            break;
    }

    return size;
}

//---------------------LogiC920---------------
LogiC920::LogiC920(int32_t id, int32_t facing, int32_t orientation, char* path)
    : UvcDevice(id, facing, orientation, path, false)
{
    mC920Stream = new C920Stream(this, path);
    mVideoStream = mC920Stream;
}

LogiC920::~LogiC920()
{
}

status_t LogiC920::initSensorStaticData()
{
    int32_t ret = UvcDevice::initSensorStaticData();
    mC920Stream->setOmitSize(mPreviewResolutions[0], mPreviewResolutions[1]);

    return ret;
}

// LogiC920 output the first several frames which are damaged.
// the mOmitFrames count on specific sensor.
LogiC920::C920Stream::C920Stream(Camera* device, const char* name)
    : UvcDevice::UvcStream(device, name), mOmitFrames(0), mOmitFrameCnt(1)
{
}
LogiC920::C920Stream::~C920Stream()
{
}

void LogiC920::C920Stream::setOmitSize(uint32_t width, uint32_t height)
{
    mOmitFrameWidth = width;
    mOmitFrameHeight = height;
}

int32_t LogiC920::C920Stream::onDeviceStartLocked()
{
    ALOGI("%s", __func__);
    int32_t ret = UvcDevice::UvcStream::onDeviceStartLocked();
    mOmitFrames = mOmitFrameCnt;
    return ret;
}

int32_t LogiC920::C920Stream::onFrameAcquireLocked()
{
    ALOGV("%s", __func__);
    int32_t index = UvcDevice::UvcStream::onFrameAcquireLocked();

    // large resolution should return immediately because of low frame rate.
    if (mWidth > mOmitFrameWidth && mHeight > mOmitFrameHeight) {
        return index;
    }

    while (mOmitFrames > 0) {
        mOmitFrames--;
        UvcDevice::UvcStream::onFrameReturnLocked(index, *mBuffers[index]);
        index = UvcDevice::UvcStream::onFrameAcquireLocked();
    }

    return index;
}

