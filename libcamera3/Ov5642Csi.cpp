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

#include "Ov5642Csi.h"

Ov5642Csi::Ov5642Csi(int32_t id, int32_t facing, int32_t orientation, char* path)
    : Camera(id, facing, orientation, path)
{
    mVideoStream = new OvStream(this);
}

Ov5642Csi::~Ov5642Csi()
{
}

status_t Ov5642Csi::initSensorStaticData()
{
    int32_t fd = open(mDevPath, O_RDWR);
    if (fd < 0) {
        ALOGE("OvDevice: initParameters sensor has not been opened");
        return BAD_VALUE;
    }

    // first read sensor format.
    int ret = 0, index = 0;
    int sensorFormats[MAX_SENSOR_FORMAT];
    int availFormats[MAX_SENSOR_FORMAT];
    memset(sensorFormats, 0, sizeof(sensorFormats));
    memset(availFormats, 0, sizeof(availFormats));
#if 0
    struct v4l2_fmtdesc vid_fmtdesc;
    while (ret == 0) {
        vid_fmtdesc.index = index;
        vid_fmtdesc.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ret               = ioctl(fd, VIDIOC_ENUM_FMT, &vid_fmtdesc);
        ALOGV("index:%d,ret:%d, format:%c%c%c%c", index, ret,
                vid_fmtdesc.pixelformat & 0xFF,
                (vid_fmtdesc.pixelformat >> 8) & 0xFF,
                (vid_fmtdesc.pixelformat >> 16) & 0xFF,
                (vid_fmtdesc.pixelformat >> 24) & 0xFF);
        if (ret == 0) {
            sensorFormats[index++] = vid_fmtdesc.pixelformat;
        }
    }
    sensorFormats[index++] = v4l2_fourcc('B', 'L', 'O', 'B');
    sensorFormats[index++] = v4l2_fourcc('R', 'A', 'W', 'S');
#endif

    // v4l2 does not support enum format, now hard code here.
    sensorFormats[index] = v4l2_fourcc('N', 'V', '1', '2');
    availFormats[index++] = v4l2_fourcc('N', 'V', '1', '2');
    sensorFormats[index] = v4l2_fourcc('Y', 'V', '1', '2');
    availFormats[index++] = v4l2_fourcc('Y', 'V', '1', '2');
    mSensorFormatCount = changeSensorFormats(sensorFormats, mSensorFormats, index);
    if (mSensorFormatCount == 0) {
        ALOGE("%s no sensor format enum", __func__);
        close(fd);
        return BAD_VALUE;
    }

    availFormats[index++] = v4l2_fourcc('N', 'V', '2', '1');
    availFormats[index++] = v4l2_fourcc('B', 'L', 'O', 'B');
    availFormats[index++] = v4l2_fourcc('R', 'A', 'W', 'S');
    //availFormats[2] = v4l2_fourcc('Y', 'U', 'Y', 'V');
    mAvailableFormatCount = changeSensorFormats(availFormats, mAvailableFormats, index);

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
        ALOGV("enum frame size w:%d, h:%d",
                vid_frmsize.discrete.width, vid_frmsize.discrete.height);
#if 0
        memset(&vid_frmval, 0, sizeof(struct v4l2_frmivalenum));
        vid_frmval.index        = 0;
        vid_frmval.pixel_format = vid_frmsize.pixel_format;
        vid_frmval.width        = vid_frmsize.discrete.width;
        vid_frmval.height       = vid_frmsize.discrete.height;

        ret = ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &vid_frmval);
        if (ret != 0) {
            continue;
        }
        ALOGV("vid_frmval denominator:%d, numeraton:%d",
                vid_frmval.discrete.denominator,
                vid_frmval.discrete.numerator);
#endif
        //v4l2 does not support, now hard code here.
        if ((vid_frmsize.discrete.width > 1280) ||
                (vid_frmsize.discrete.height > 800)) {
            vid_frmval.discrete.denominator = 15;
            vid_frmval.discrete.numerator   = 1;
        }
        else {
            vid_frmval.discrete.denominator = 30;
            vid_frmval.discrete.numerator   = 1;
        }

        //If w/h ratio is not same with senserW/sensorH, framework assume that
        //first crop little width or little height, then scale.
        //But 1920x1080, 176x144 not work in this mode.
        //For 1M pixel, 720p sometimes may take green picture(5%), so not report it,
        //use 1024x768 for 1M pixel
        // 1920x1080 1280x720 is required by CTS.
        if(!(vid_frmsize.discrete.width == 176 && vid_frmsize.discrete.height == 144)) {
            mPictureResolutions[pictureCnt++] = vid_frmsize.discrete.width;
            mPictureResolutions[pictureCnt++] = vid_frmsize.discrete.height;
        }

        if (vid_frmval.discrete.denominator / vid_frmval.discrete.numerator > 15) {
            mPreviewResolutions[previewCnt++] = vid_frmsize.discrete.width;
            mPreviewResolutions[previewCnt++] = vid_frmsize.discrete.height;;
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
    mTargetFpsRange[i++] = 15;
    mTargetFpsRange[i++] = 30;
    mTargetFpsRange[i++] = 30;
    mTargetFpsRange[i++] = 30;

    setMaxPictureResolutions();
    ALOGI("mMaxWidth:%d, mMaxHeight:%d", mMaxWidth, mMaxHeight);

    mFocalLength = 3.37f;
    mPhysicalWidth = 3.6288f;   //2592 x 1.4u
    mPhysicalHeight = 2.7216f;  //1944 x 1.4u
    mActiveArrayWidth = 2592;
    mActiveArrayHeight = 1944;
    mPixelArrayWidth = 2592;
    mPixelArrayHeight = 1944;

    ALOGI("ov5642Csi, mFocalLength:%f, mPhysicalWidth:%f, mPhysicalHeight %f",
        mFocalLength, mPhysicalWidth, mPhysicalHeight);

    close(fd);
    return NO_ERROR;
}

uint8_t Ov5642Csi::getAutoFocusStatus(uint8_t mode)
{
    struct v4l2_control c;
    uint8_t ret = ANDROID_CONTROL_AF_STATE_INACTIVE;
    int result;

    int32_t fd = open(mDevPath, O_RDWR);
    if (fd < 0) {
        ALOGE("couldn't open device %s", mDevPath);
        return ret;
    }

    c.id = V4L2_CID_AUTO_FOCUS_STATUS;
    result = ioctl(fd, VIDIOC_G_CTRL, &c);
    if (result != 0) {
        ALOGE("ioctl error: %d", result);
        goto end;
    }

    switch (c.value) {
    case V4L2_AUTO_FOCUS_STATUS_BUSY:
        if ((mode == ANDROID_CONTROL_AF_MODE_AUTO) ||
            (mode == ANDROID_CONTROL_AF_MODE_MACRO))
            ret = ANDROID_CONTROL_AF_STATE_ACTIVE_SCAN;
        else
            ret = ANDROID_CONTROL_AF_STATE_PASSIVE_SCAN;
        break;
    case V4L2_AUTO_FOCUS_STATUS_REACHED:
        ret = ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED;
        break;
    case V4L2_AUTO_FOCUS_STATUS_FAILED:
    case V4L2_AUTO_FOCUS_STATUS_IDLE:
    default:
        ret = ANDROID_CONTROL_AF_STATE_INACTIVE;
    }
end:
    close(fd);
    return ret;
}

#define OV5640_AF_ZONE_ARRAY_WIDTH	80
void Ov5642Csi::setAutoFocusRegion(int x, int y)
{
    struct v4l2_control c;
    int result;
    /* Android provides coordinates scaled to max picture resolution */
    float ratio = (float)mVideoStream->getWidth() / mVideoStream->getHeight();
    int scaled_x = x / (mMaxWidth / OV5640_AF_ZONE_ARRAY_WIDTH);
    int scaled_y = y / (mMaxHeight / (OV5640_AF_ZONE_ARRAY_WIDTH / ratio));

    int32_t fd = open(mDevPath, O_RDWR);
    if (fd < 0) {
        ALOGE("couldn't open device %s", mDevPath);
        return;
    }

    /* Using custom implementation of the absolute focus ioctl for ov5640 */
    c.id = V4L2_CID_FOCUS_ABSOLUTE;
    c.value = ((scaled_x & 0xFFFF) << 16) + (scaled_y & 0xFFFF);
    result = ioctl(fd, VIDIOC_S_CTRL, &c);
    if (result != 0)
        ALOGE("ioctl error: %d", result);

    close(fd);
    return;
}

uint8_t Ov5642Csi::doAutoFocus(uint8_t mode)
{
    struct v4l2_control c;
    uint8_t ret = ANDROID_CONTROL_AF_STATE_INACTIVE;
    int result;

    int32_t fd = open(mDevPath, O_RDWR);
    if (fd < 0) {
        ALOGE("couldn't open device %s", mDevPath);
        return ret;
    }

    switch (mode) {
    case ANDROID_CONTROL_AF_MODE_AUTO:
    case ANDROID_CONTROL_AF_MODE_MACRO:
        ret = ANDROID_CONTROL_AF_STATE_ACTIVE_SCAN;
        c.id = V4L2_CID_AUTO_FOCUS_START;
        break;
    case ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO:
    case ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE:
        ret = ANDROID_CONTROL_AF_STATE_PASSIVE_SCAN;
        c.id = V4L2_CID_FOCUS_AUTO;
        c.value = 1;
        break;
    case ANDROID_CONTROL_AF_MODE_OFF:
    default:
        ret = ANDROID_CONTROL_AF_STATE_INACTIVE;
        c.id = V4L2_CID_AUTO_FOCUS_STOP;
    }
    result = ioctl(fd, VIDIOC_S_CTRL, &c);
    if (result != 0) {
        ALOGE("ioctl error: %d", result);
        ret = ANDROID_CONTROL_AF_STATE_INACTIVE;
    }

    close(fd);

    return ret;
}

// configure device.
int32_t Ov5642Csi::OvStream::onDeviceConfigureLocked()
{
    ALOGI("%s", __func__);
    int32_t ret = 0;
    if (mDev <= 0) {
        ALOGE("%s invalid fd handle", __func__);
        return BAD_VALUE;
    }

    int32_t input = 1;
    ret = ioctl(mDev, VIDIOC_S_INPUT, &input);
    if (ret < 0) {
        ALOGE("%s VIDIOC_S_INPUT Failed: %s", __func__, strerror(errno));
        return ret;
    }

    return MMAPStream::onDeviceConfigureLocked();
}

