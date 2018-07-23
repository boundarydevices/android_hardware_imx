/*
 * Copyright 2018 NXP
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Ov5640Csi7D.h"

Ov5640Csi7D::Ov5640Csi7D(int32_t id,
                         int32_t facing,
                         int32_t orientation,
                         char* path)
        : Camera(id, facing, orientation, path)
{
    mVideoStream = new OvStream(this);

    // Sometimes after stream on, the first 2 frames may capture black.
    // Omit them to workaround.
    mVideoStream->setOmitFrameCount(2);
}

Ov5640Csi7D::~Ov5640Csi7D() {}

PixelFormat Ov5640Csi7D::getPreviewPixelFormat()
{
    return HAL_PIXEL_FORMAT_RGBA_8888;
}

int Ov5640Csi7D::getCaptureMode(int width, int height)
{
    int capturemode = -1;

    if ((width == 640) && (height == 480)) {
        capturemode = 0;
    } else if ((width == 720) && (height == 480)) {
        capturemode = 1;
    } else if ((width == 1280) && (height == 720)) {
        capturemode = 2;
    } else {
        ALOGE("%s width:%d height:%d is not supported.", __func__, width, height);
    }
    return capturemode;
}

status_t Ov5640Csi7D::initSensorStaticData()
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
    if (usemx6s > 0) {
        struct v4l2_fmtdesc vid_fmtdesc;
        while (ret == 0) {
            vid_fmtdesc.index = index;
            vid_fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ret = ioctl(fd, VIDIOC_ENUM_FMT, &vid_fmtdesc);
            ALOGI("index:%d,ret:%d, format:%c%c%c%c",
                        index,
                        ret,
                        vid_fmtdesc.pixelformat & 0xFF,
                        (vid_fmtdesc.pixelformat >> 8) & 0xFF,
                        (vid_fmtdesc.pixelformat >> 16) & 0xFF,
                        (vid_fmtdesc.pixelformat >> 24) & 0xFF);
            if (ret == 0) {
                sensorFormats[index] = vid_fmtdesc.pixelformat;
                availFormats[index++] = vid_fmtdesc.pixelformat;
            }
        }

        mSensorFormatCount =
                changeSensorFormats(sensorFormats, mSensorFormats, index);
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
        mAvailableFormatCount =
                changeSensorFormats(availFormats, mAvailableFormats, index);

    } else {
        // v4l2 does not support enum format, now hard code here.
        sensorFormats[index] = v4l2_fourcc('N', 'V', '1', '2');
        availFormats[index++] = v4l2_fourcc('N', 'V', '1', '2');
        sensorFormats[index] = v4l2_fourcc('Y', 'V', '1', '2');
        availFormats[index++] = v4l2_fourcc('Y', 'V', '1', '2');
        mSensorFormatCount =
                changeSensorFormats(sensorFormats, mSensorFormats, index);
        if (mSensorFormatCount == 0) {
            ALOGE("%s no sensor format enum", __func__);
            close(fd);
            return BAD_VALUE;
        }

        availFormats[index++] = v4l2_fourcc('N', 'V', '2', '1');
        availFormats[index++] = v4l2_fourcc('B', 'L', 'O', 'B');
        availFormats[index++] = v4l2_fourcc('R', 'A', 'W', 'S');
        // availFormats[2] = v4l2_fourcc('Y', 'U', 'Y', 'V');
        mAvailableFormatCount =
                changeSensorFormats(availFormats, mAvailableFormats, index);
    }

    ret = 0;
    index = 0;
    char TmpStr[20];
    int previewCnt = 0, pictureCnt = 0;
    struct v4l2_frmsizeenum vid_frmsize;
    struct v4l2_frmivalenum vid_frmval;
    while (ret == 0) {
        memset(TmpStr, 0, 20);
        memset(&vid_frmsize, 0, sizeof(struct v4l2_frmsizeenum));
        vid_frmsize.index = index++;
        vid_frmsize.pixel_format =
                convertPixelFormatToV4L2Format(mSensorFormats[0]);
        ret = ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &vid_frmsize);
        if (ret != 0) {
            continue;
        }

        // 2592x1944 yuyv is about 10MB, 3 vl42 buffers will cause 30MB.
        // After pico-imx7d boot, only about 35MB free. Allocate v4l2 buffers
        // failed. 176x144 may block on dqbuf due to driver issue. Also iMX7D
        // devices have not enough memory for 1920x1080 resolution.
        if (((vid_frmsize.discrete.width == 2592) &&
                 (vid_frmsize.discrete.height == 1944)) ||
                ((vid_frmsize.discrete.width == 1920) &&
                 (vid_frmsize.discrete.height == 1080)) ||
                ((vid_frmsize.discrete.width == 176) &&
                 (vid_frmsize.discrete.height == 144))) {
            continue;
        }

        ALOGI("enum frame size w:%d, h:%d, fmt 0x%x",
                    vid_frmsize.discrete.width,
                    vid_frmsize.discrete.height,
                    vid_frmsize.pixel_format);
        if (usemx6s > 0) {
            memset(&vid_frmval, 0, sizeof(struct v4l2_frmivalenum));
            vid_frmval.index = 0;
            vid_frmval.pixel_format = vid_frmsize.pixel_format;
            vid_frmval.width = vid_frmsize.discrete.width;
            vid_frmval.height = vid_frmsize.discrete.height;

            ret = ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &vid_frmval);
            if (ret != 0) {
                continue;
            }
            ALOGI("vid_frmval denominator:%d, numeraton:%d",
                        vid_frmval.discrete.denominator,
                        vid_frmval.discrete.numerator);
        } else { // v4l2 does not support, now hard code here.
            if ((vid_frmsize.discrete.width > 1280) ||
                    (vid_frmsize.discrete.height > 800)) {
                vid_frmval.discrete.denominator = 15;
                vid_frmval.discrete.numerator = 1;
            } else if ((vid_frmsize.discrete.width == 1024) ||
                                 (vid_frmsize.discrete.height == 768)) {
                // Max fps for ov5640 csi xga cannot reach to 30fps
                vid_frmval.discrete.denominator = 15;
                vid_frmval.discrete.numerator = 1;
            } else {
                vid_frmval.discrete.denominator = 30;
                vid_frmval.discrete.numerator = 1;
            }
        }
        // If w/h ratio is not same with senserW/sensorH, framework assume that
        // first crop little width or little height, then scale.
        // But 1920x1080, 176x144 not work in this mode.
        // 1920x1080 is required by CTS.
        if (usemx6s > 0) {
            mPictureResolutions[pictureCnt++] = vid_frmsize.discrete.width;
            mPictureResolutions[pictureCnt++] = vid_frmsize.discrete.height;

            if (vid_frmval.discrete.denominator / vid_frmval.discrete.numerator >
                    15) {
                mPreviewResolutions[previewCnt++] = vid_frmsize.discrete.width;
                mPreviewResolutions[previewCnt++] = vid_frmsize.discrete.height;
            }
        } else {
            if (!(vid_frmsize.discrete.width == 176 &&
                        vid_frmsize.discrete.height == 144)) {
                mPictureResolutions[pictureCnt++] = vid_frmsize.discrete.width;
                mPictureResolutions[pictureCnt++] = vid_frmsize.discrete.height;
            }

            if (vid_frmval.discrete.denominator / vid_frmval.discrete.numerator >
                    15) {
                mPreviewResolutions[previewCnt++] = vid_frmsize.discrete.width;
                mPreviewResolutions[previewCnt++] = vid_frmsize.discrete.height;
            }
        }

    }  // end while

    mPreviewResolutionCount = previewCnt;
    mPictureResolutionCount = pictureCnt;

    mMinFrameDuration = 33331760L;
    mMaxFrameDuration = 30000000000L;
    int i;
    for (i = 0; i < MAX_RESOLUTION_SIZE && i < pictureCnt; i += 2) {
        ALOGI("SupportedPictureSizes: %d x %d",
                    mPictureResolutions[i],
                    mPictureResolutions[i + 1]);
    }

    adjustPreviewResolutions();
    for (i = 0; i < MAX_RESOLUTION_SIZE && i < previewCnt; i += 2) {
        ALOGI("SupportedPreviewSizes: %d x %d",
                    mPreviewResolutions[i],
                    mPreviewResolutions[i + 1]);
    }
    ALOGI("FrameDuration is %" PRId64 ", %" PRId64 "", mMinFrameDuration, mMaxFrameDuration);

    mTargetFpsRange[0] = 15;
    mTargetFpsRange[1] = 30;
    mTargetFpsRange[2] = 30;
    mTargetFpsRange[3] = 30;

    setMaxPictureResolutions();
    ALOGI("mMaxWidth:%d, mMaxHeight:%d", mMaxWidth, mMaxHeight);

    mFocalLength = 3.37f;
    mPhysicalWidth = 3.6288f;       // 2592 x 1.4u
    mPhysicalHeight = 2.7216f;  // 1944 x 1.4u
    mActiveArrayWidth = 2592;
    mActiveArrayHeight = 1944;
    mPixelArrayWidth = 2592;
    mPixelArrayHeight = 1944;
    mMaxJpegSize = 2073600; // 1920*1080

    ALOGI("ov5640Csi, mFocalLength:%f, mPhysicalWidth:%f, mPhysicalHeight %f, mMaxJpegSize %d",
                mFocalLength,
                mPhysicalWidth,
                mPhysicalHeight,
                mMaxJpegSize);

    close(fd);
    return NO_ERROR;
}

// configure device.
int32_t Ov5640Csi7D::OvStream::onDeviceConfigureLocked()
{
    ALOGI("%s", __func__);
    if (mDev <= 0) {
            ALOGE("%s invalid fd handle", __func__);
            return BAD_VALUE;
    }

    int32_t input = 1;
    // don't get ioctl return value.
    // only call this ioctl.
    int ret = ioctl(mDev, VIDIOC_S_INPUT, &input);
    if (ret < 0) {
            ALOGW("%s VIDIOC_S_INPUT not supported: %s", __func__, strerror(errno));
            mCustomDriver = false;
    }
    else {
            mCustomDriver = true;
    }

    return MMAPStream::onDeviceConfigureLocked();
}
