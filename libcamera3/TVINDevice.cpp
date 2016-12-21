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

#include "TVINDevice.h"
#include "USPStream.h"

TVINDevice::TVINDevice(int32_t id, int32_t facing, int32_t orientation, char* path)
    : Camera(id, facing, orientation, path)
{
    mVideoStream = new TVinStream(this);
}

TVINDevice::~TVINDevice()
{
}

status_t TVINDevice::initSensorStaticData()
{
    int32_t fd = open(mDevPath, O_RDWR);
    if (fd < 0) {
        ALOGE("OvDevice: initParameters sensor has not been opened");
        return BAD_VALUE;
    }

    int ret = 0, index = 0;
    int maxWait = 6;
    // Get the PAL/NTSC STD
    do {
        ret = ioctl(fd, VIDIOC_G_STD, &mSTD);
        if (ret < 0) {
            ALOGW("%s VIDIOC_G_STD failed with try %d", __func__, maxWait - 1);
            sleep(1);
        }
        maxWait --;
    } while ((ret != 0) && (maxWait > 0));

    if (mSTD == V4L2_STD_PAL)
        ALOGI("%s Get current mode: PAL", __func__);
    else if (mSTD == V4L2_STD_NTSC)
        ALOGI("%s Get current mode: NTSC", __func__);
    else {
        ALOGE("%s Error!Get invalid mode: %llu", __func__, mSTD);
        close(fd);
        return BAD_VALUE;
    }

    if (ioctl(fd, VIDIOC_S_STD, &mSTD) < 0) {
        ALOGE("%s VIDIOC_S_STD failed", __func__);
        close(fd);
        return BAD_VALUE;
    }

    // read sensor format.
    int sensorFormats[MAX_SENSOR_FORMAT];
    int availFormats[MAX_SENSOR_FORMAT];
    memset(sensorFormats, 0, sizeof(sensorFormats));
    memset(availFormats, 0, sizeof(availFormats));

    // v4l2 does not support VIDIOC_ENUM_FMT, now hard code here.
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

        ALOGV("enum frame size w:%d, h:%d",
                vid_frmsize.discrete.width, vid_frmsize.discrete.height);
        //memset(&vid_frmval, 0, sizeof(struct v4l2_frmivalenum));
        //vid_frmval.index        = 0;
        //vid_frmval.pixel_format = vid_frmsize.pixel_format;
        //vid_frmval.width        = vid_frmsize.discrete.width;
        //vid_frmval.height       = vid_frmsize.discrete.height;

        // ret = ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &vid_frmval);
        //if (ret != 0) {
        //    continue;
        //}
        //ALOGV("vid_frmval denominator:%d, numeraton:%d",
        //        vid_frmval.discrete.denominator,
        //        vid_frmval.discrete.numerator);

        // v4l2 does not support VIDIOC_ENUM_FRAMEINTERVALS, now hard code here.
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
        if(!(vid_frmsize.discrete.width == 176 && vid_frmsize.discrete.height == 144)){
            mPictureResolutions[pictureCnt++] = vid_frmsize.discrete.width;
            mPictureResolutions[pictureCnt++] = vid_frmsize.discrete.height;
        }

        if (vid_frmval.discrete.denominator / vid_frmval.discrete.numerator > 15) {
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

    mFocalLength = 3.37f;
    mPhysicalWidth = 3.6288f;   //2592 x 1.4u
    mPhysicalHeight = 2.7216f;  //1944 x 1.4u
    mActiveArrayWidth = 720;
    mActiveArrayHeight = 576;
    mPixelArrayWidth = 720;
    mPixelArrayHeight = 576;

    ALOGI("tvin device, mFocalLength:%f, mPhysicalWidth:%f, mPhysicalHeight %f",
        mFocalLength, mPhysicalWidth, mPhysicalHeight);

    close(fd);
    return NO_ERROR;
}

// configure device.
int32_t TVINDevice::TVinStream::onDeviceConfigureLocked()
{
    ALOGI("%s", __func__);
    int32_t ret = 0;
    if (mDev <= 0) {
        ALOGE("%s invalid fd handle", __func__);
        return BAD_VALUE;
    }

    int maxWait = 6;
    v4l2_std_id mSTD;
    // Get the PAL/NTSC STD
    do {
        ret = ioctl(mDev, VIDIOC_G_STD, &mSTD);
        if (ret < 0) {
            ALOGW("%s VIDIOC_G_STD failed with try %d", __func__, maxWait - 1);
            sleep(1);
        }
        maxWait --;
    } while ((ret != 0) && (maxWait > 0));

    if (mSTD == V4L2_STD_PAL)
        ALOGI("%s Get current mode: PAL", __func__);
    else if (mSTD == V4L2_STD_NTSC)
        ALOGI("%s Get current mode: NTSC", __func__);
    else {
        ALOGE("%s Error!Get invalid mode: %llu", __func__, mSTD);
        return BAD_VALUE;
    }

    if (ioctl(mDev, VIDIOC_S_STD, &mSTD) < 0) {
        ALOGE("%s VIDIOC_S_STD failed", __func__);
        return BAD_VALUE;
    }

    int32_t input = 1;
    ret = ioctl(mDev, VIDIOC_S_INPUT, &input);
    if (ret < 0) {
        ALOGE("%s VIDIOC_S_INPUT Failed: %s", __func__, strerror(errno));
        return ret;
    }

    return USPStream::onDeviceConfigureLocked();
}

