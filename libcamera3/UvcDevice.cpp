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

//----------------------UvcDevice--------------------
Camera* UvcDevice::newInstance(int32_t id, char* name, int32_t facing,
                               int32_t orientation, char* path,
                               CscHw cam_copy_hw, CscHw cam_csc_hw, const char *hw_jpeg_enc,
                                 CameraSensorMetadata *cam_metadata)
{
    ALOGI("%s usb sensor name:%s", __func__, name);
    UvcDevice* device = NULL;

    ALOGI("%s usb sensor:%s use standard UVC device", __func__, name);
    device = new UvcDevice(id, facing, orientation, path, cam_copy_hw, cam_csc_hw, hw_jpeg_enc, true, cam_metadata);

    return device;
}

UvcDevice::UvcDevice(int32_t id, int32_t facing, int32_t orientation,
                     char* path, CscHw cam_copy_hw, CscHw cam_csc_hw, const char *hw_jpeg_enc, bool createStream, CameraSensorMetadata *cam_metadata)
    : Camera(id, facing, orientation, path, cam_copy_hw, cam_csc_hw, hw_jpeg_enc)
{
    mCameraMetadata = cam_metadata;

    if (createStream) {
        mVideoStream = new UvcStream(this, path, cam_metadata->omit_frame);
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

    mMinFrameDuration = mCameraMetadata->minframeduration;
    mMaxFrameDuration = mCameraMetadata->maxframeduration;
    int i;
    for (i=0; i<MAX_RESOLUTION_SIZE && i<pictureCnt; i+=2) {
        ALOGI("SupportedPictureSizes: %d x %d", mPictureResolutions[i], mPictureResolutions[i+1]);
    }

    adjustPreviewResolutions();
    for (i=0; i<MAX_RESOLUTION_SIZE && i<previewCnt; i+=2) {
        ALOGI("SupportedPreviewSizes: %d x %d", mPreviewResolutions[i], mPreviewResolutions[i+1]);
    }
    ALOGI("FrameDuration is %" PRId64 ", %" PRId64 "", mMinFrameDuration, mMaxFrameDuration);

    i = 0;
    mTargetFpsRange[i++] = 10;
    mTargetFpsRange[i++] = 30;
    mTargetFpsRange[i++] = 30;
    mTargetFpsRange[i++] = 30;

    setMaxPictureResolutions();
    ALOGI("mMaxWidth:%d, mMaxHeight:%d", mMaxWidth, mMaxHeight);
    mFocalLength = mCameraMetadata->focallength;
    mPhysicalWidth = mCameraMetadata->physicalwidth;
    mPhysicalHeight = mCameraMetadata->physicalheight;
    mActiveArrayWidth = mCameraMetadata->activearraywidth;
    mActiveArrayHeight = mCameraMetadata->activearrayheight;
    mPixelArrayWidth = mCameraMetadata->pixelarraywidth;
    mPixelArrayHeight = mCameraMetadata->pixelarrayheight;

    ALOGI("UvcDevice, mFocalLength:%f, mPhysicalWidth:%f, mPhysicalHeight %f",
        mFocalLength, mPhysicalWidth, mPhysicalHeight);

    close(fd);
    return 0;
}

int32_t UvcDevice::UvcStream::onDeviceConfigureLocked()
{
    ALOGI("%s", __func__);

    if (mDev <= 0) {
        // usb camera should open dev node again.
        // because when stream off, the dev node must close.
        mDev = open(mUvcPath, O_RDWR);
        if (mDev <= 0) {
            ALOGE("%s invalid fd handle", __func__);
            return BAD_VALUE;
        }
    }

    setOmitFrameCount(0);

    struct OmitFrame *item;
    for(item = mOmitFrame; item < mOmitFrame + OMIT_RESOLUTION_NUM; item++) {
      if ((mWidth == item->width) && (mHeight == item->height)) {
        setOmitFrameCount(item->omitnum);
        break;
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
