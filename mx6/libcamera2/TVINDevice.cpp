/*
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

#include "CameraUtil.h"
#include "TVINDevice.h"

status_t TVINDevice::changeSensorFormats(int *src, int len)
{
    if (src == NULL || len == 0) {
        return 0;
    }

    int k = 0;
    for (int i=0; i<len && i<MAX_SENSOR_FORMAT; i++) {
        switch (src[i]) {
            case v4l2_fourcc('N', 'V', '1', '2'):
                mAvailableFormats[k++] = HAL_PIXEL_FORMAT_YCbCr_420_SP;
                break;

            case v4l2_fourcc('Y', 'V', '1', '2'):
                mAvailableFormats[k++] = HAL_PIXEL_FORMAT_YCbCr_420_P;
                break;

            case v4l2_fourcc('Y', 'U', 'Y', 'V'):
                mAvailableFormats[k++] = HAL_PIXEL_FORMAT_YCbCr_422_I;
                break;

            case v4l2_fourcc('B', 'L', 'O', 'B'):
                mAvailableFormats[k++] = HAL_PIXEL_FORMAT_BLOB;
                break;

            case v4l2_fourcc('R', 'A', 'W', 'S'):
                mAvailableFormats[k++] = HAL_PIXEL_FORMAT_RAW_SENSOR;
                break;

            default:
                FLOGE("Error: format 0x%x not supported!", src[i]);
                break;
        }
    }

    return 0;
}

status_t TVINDevice::initSensorInfo(const CameraInfo& info)
{
    if (mCameraHandle < 0) {
        FLOGE("TVINDevice: initParameters sensor has not been opened");
        return BAD_VALUE;
    }

    int res = 0;
    int maxWait = 6;
    // Get the PAL/NTSC STD
    do {
        res = ioctl(mCameraHandle, VIDIOC_G_STD, &mSTD);
        if (res < 0) {
            FLOGE("VIDIOC_G_STD failed with more try %d\n", maxWait - 1);
            sleep(1);
        }
        maxWait --;
    }while ((res != 0) || (maxWait <= 0));

    if (mSTD == V4L2_STD_PAL)
        FLOGI("Get current mode: PAL");
    else if (mSTD == V4L2_STD_NTSC)
        FLOGI("Get current mode: NTSC");
    else {
        FLOGI("Error!Get invalid mode: %llu", mSTD);
        return BAD_VALUE;
    }

    if (ioctl(mCameraHandle, VIDIOC_S_STD, &mSTD) < 0) {
        FLOGE("VIDIOC_S_STD failed\n");
        return BAD_VALUE;
    }


    // first read sensor format.
    int ret = 0, index = 0;
    int sensorFormats[MAX_SENSOR_FORMAT];
    memset(mAvailableFormats, 0, sizeof(mAvailableFormats));
    memset(sensorFormats, 0, sizeof(sensorFormats));
#if 0
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
    sensorFormats[index++] = v4l2_fourcc('B', 'L', 'O', 'B');
    sensorFormats[index++] = v4l2_fourcc('R', 'A', 'W', 'S');
#endif

    // v4l2 does not support enum format, now hard code here.
    sensorFormats[index++] = v4l2_fourcc('N', 'V', '1', '2');
    sensorFormats[index++] = v4l2_fourcc('Y', 'V', '1', '2');
    sensorFormats[index++] = v4l2_fourcc('B', 'L', 'O', 'B');
    sensorFormats[index++] = v4l2_fourcc('R', 'A', 'W', 'S');
    //mAvailableFormats[2] = v4l2_fourcc('Y', 'U', 'Y', 'V');
    mAvailableFormatCount = index;
    changeSensorFormats(sensorFormats, index);

    index = 0;
    char TmpStr[20];
    int  previewCnt = 0, pictureCnt = 0;
    struct v4l2_frmsizeenum vid_frmsize;
    struct v4l2_frmivalenum vid_frmval;
    while (ret == 0) {
        memset(TmpStr, 0, 20);
        memset(&vid_frmsize, 0, sizeof(struct v4l2_frmsizeenum));
        vid_frmsize.index        = index++;
        vid_frmsize.pixel_format = v4l2_fourcc('N', 'V', '1', '2');
        ret = ioctl(mCameraHandle,
                    VIDIOC_ENUM_FRAMESIZES, &vid_frmsize);
        if (ret == 0) {
            FLOG_RUNTIME("enum frame size w:%d, h:%d",
                         vid_frmsize.discrete.width, vid_frmsize.discrete.height);
            memset(&vid_frmval, 0, sizeof(struct v4l2_frmivalenum));
            vid_frmval.index        = 0;
            vid_frmval.pixel_format = vid_frmsize.pixel_format;
            vid_frmval.width        = vid_frmsize.discrete.width;
            vid_frmval.height       = vid_frmsize.discrete.height;

            // ret = ioctl(mCameraHandle, VIDIOC_ENUM_FRAMEINTERVALS,
            // &vid_frmval);
            // v4l2 does not support, now hard code here.
            if (ret == 0) {
                FLOG_RUNTIME("vid_frmval denominator:%d, numeraton:%d",
                             vid_frmval.discrete.denominator,
                             vid_frmval.discrete.numerator);
                if ((vid_frmsize.discrete.width > 1920) ||
                    (vid_frmsize.discrete.height > 1080)) {
                    vid_frmval.discrete.denominator = 15;
                    vid_frmval.discrete.numerator   = 1;
                }
                else {
                    vid_frmval.discrete.denominator = 30;
                    vid_frmval.discrete.numerator   = 1;
                }

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
    mFocalLength = 10.001;

    return NO_ERROR;
}

int TVINDevice::getCaptureMode(int width, int height)
{
    return 0;
}

status_t TVINDevice::adjustPreviewResolutions()
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

status_t TVINDevice::setMaxPictureResolutions()
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

