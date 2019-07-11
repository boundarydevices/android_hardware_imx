/*
 * Copyright 2018 NXP.
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

#include "Ov5640Imx8Q.h"

Ov5640Imx8Q::Ov5640Imx8Q(int32_t id, int32_t facing, int32_t orientation, char *path)
    : Camera(id, facing, orientation, path)
{
    mVideoStream = new Ov5640Stream(this);
}

Ov5640Imx8Q::~Ov5640Imx8Q()
{
}


int Ov5640Imx8Q::Ov5640Stream::getCaptureMode(int width, int height)
{
    int index = 0;
    int ret = 0;
    int capturemode = 0;
    struct v4l2_frmsizeenum vid_frmsize;

    while (ret == 0) {
        vid_frmsize.index = index++;
        vid_frmsize.pixel_format = v4l2_fourcc('Y', 'U', 'Y', 'V');
        ret = ioctl(mDev, VIDIOC_ENUM_FRAMESIZES, &vid_frmsize);
        if ((vid_frmsize.discrete.width == (uint32_t)width) && (vid_frmsize.discrete.height == (uint32_t)height)
            && (ret == 0)) {
            capturemode = vid_frmsize.index;
            break;
        }
    }

    return capturemode;
}

status_t Ov5640Imx8Q::initSensorStaticData()
{
    int32_t fd = open(mDevPath, O_RDWR);
    if (fd < 0) {
        ALOGE("Ov5640CameraDevice: initParameters sensor has not been opened");
        return BAD_VALUE;
    }

    // first read sensor format.
    int ret = 0, index = 0;
    int sensorFormats[MAX_SENSOR_FORMAT];
    int availFormats[MAX_SENSOR_FORMAT];
    memset(sensorFormats, 0, sizeof(sensorFormats));
    memset(availFormats, 0, sizeof(availFormats));

    // Don't support enum format, now hard code here.
    sensorFormats[index] = v4l2_fourcc('Y', 'U', 'Y', 'V');
    availFormats[index++] = v4l2_fourcc('Y', 'U', 'Y', 'V');
    mSensorFormatCount =
        changeSensorFormats(sensorFormats, mSensorFormats, index);
    if (mSensorFormatCount == 0) {
        ALOGE("%s no sensor format enum", __func__);
        close(fd);
        return BAD_VALUE;
    }

    availFormats[index++] = v4l2_fourcc('N', 'V', '2', '1');
    mAvailableFormatCount =
        changeSensorFormats(availFormats, mAvailableFormats, index);

    index = 0;
    char TmpStr[20];
    uint32_t fps = 0;
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
        ALOGI("enum frame size w:%d, h:%d", vid_frmsize.discrete.width, vid_frmsize.discrete.height);

        if (vid_frmsize.discrete.width == 0 ||
              vid_frmsize.discrete.height == 0) {
            continue;
        }

        vid_frmval.index = 0;
        vid_frmval.pixel_format = vid_frmsize.pixel_format;
        vid_frmval.width = vid_frmsize.discrete.width;
        vid_frmval.height = vid_frmsize.discrete.height;
        while (ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &vid_frmval) >= 0) {
            if (fps < vid_frmval.discrete.denominator / vid_frmval.discrete.numerator) {
                fps = vid_frmval.discrete.denominator / vid_frmval.discrete.numerator;
            }
            vid_frmval.index++;
        }

        // If w/h ratio is not same with senserW/sensorH, framework assume that
        // first crop little width or little height, then scale.
        // But 1920x1080, 176x144 not work in this mode.
        // For 1M pixel, 720p sometimes may take green picture(5%), so not report
        // it,
        // use 1024x768 for 1M pixel
        // 1920x1080 1280x720 is required by CTS.
        if (!(vid_frmsize.discrete.width == 176 &&
              vid_frmsize.discrete.height == 144)) {
            mPictureResolutions[pictureCnt++] = vid_frmsize.discrete.width;
            mPictureResolutions[pictureCnt++] = vid_frmsize.discrete.height;
        }

        if (fps > 15) {
            mPreviewResolutions[previewCnt++] = vid_frmsize.discrete.width;
            mPreviewResolutions[previewCnt++] = vid_frmsize.discrete.height;
        }
    }  // end while

    mPreviewResolutionCount = previewCnt;
    mPictureResolutionCount = pictureCnt;

    mMinFrameDuration = 33331760L;
    mMaxFrameDuration = 30000000000L;
    int i;
    for (i = 0; i < MAX_RESOLUTION_SIZE && i < pictureCnt; i += 2) {
        ALOGI("SupportedPictureSizes: %d x %d", mPictureResolutions[i], mPictureResolutions[i + 1]);
    }

    adjustPreviewResolutions();
    for (i = 0; i < MAX_RESOLUTION_SIZE && i < previewCnt; i += 2) {
        ALOGI("SupportedPreviewSizes: %d x %d", mPreviewResolutions[i], mPreviewResolutions[i + 1]);
    }
    ALOGI("FrameDuration is %" PRId64 ", %" PRId64 "", mMinFrameDuration, mMaxFrameDuration);

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
    mPhysicalWidth = 3.6288f;   // 2592 x 1.4u
    mPhysicalHeight = 2.7216f;  // 1944 x 1.4u
    mActiveArrayWidth = 2592;
    mActiveArrayHeight = 1944;
    mPixelArrayWidth = 2592;
    mPixelArrayHeight = 1944;
    mMaxJpegSize = 8 * 1024 * 1024;

    ALOGI("ImxdpuCsi, mFocalLength:%f, mPhysicalWidth:%f, mPhysicalHeight %f",
          mFocalLength,
          mPhysicalWidth,
          mPhysicalHeight);

    close(fd);
    return NO_ERROR;
}

PixelFormat Ov5640Imx8Q::getPreviewPixelFormat()
{
    ALOGI("%s", __func__);
    return HAL_PIXEL_FORMAT_YCbCr_422_I;
}

// configure device.
int32_t Ov5640Imx8Q::Ov5640Stream::onDeviceConfigureLocked()
{
    ALOGI("%s", __func__);
    int32_t ret = 0;
    if (mDev <= 0) {
        ALOGE("%s invalid fd handle", __func__);
        return BAD_VALUE;
    }

    uint32_t fps = 0;
    int32_t vformat;
    vformat = convertPixelFormatToV4L2Format(mFormat);

    struct v4l2_frmivalenum frmival;
    frmival.index = 0;
    frmival.pixel_format = vformat;
    frmival.width = mWidth;
    frmival.height = mHeight;

    while (ioctl(mDev, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) == 0) {
        if (fps < frmival.discrete.denominator / frmival.discrete.numerator) {
            fps = frmival.discrete.denominator / frmival.discrete.numerator;
        }
        if (mFps == (frmival.discrete.denominator / frmival.discrete.numerator)) {
            fps = mFps;
            break;
        }
        frmival.index++;
    }

    ALOGI("Width * Height %d x %d format %c%c%c%c, fps: %d", mWidth, mHeight, vformat & 0xFF, (vformat >> 8) & 0xFF,
        (vformat >> 16) & 0xFF, (vformat >> 24) & 0xFF, fps);

    struct v4l2_streamparm param;
    memset(&param, 0, sizeof(param));
    param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    param.parm.capture.timeperframe.numerator   = 1;
    param.parm.capture.timeperframe.denominator = fps;
    param.parm.capture.capturemode = getCaptureMode(mWidth, mHeight);
    ret = ioctl(mDev, VIDIOC_S_PARM, &param);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_S_PARM Failed: %s", __func__, strerror(errno));
        return ret;
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.pixelformat = vformat;
    fmt.fmt.pix_mp.width = mWidth & 0xFFFFFFF8;
    fmt.fmt.pix_mp.height = mHeight & 0xFFFFFFF8;
    fmt.fmt.pix_mp.num_planes = 1; /* ov5640 use YUYV format, is packed storage mode, set num_planes 1*/

    ret = ioctl(mDev, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_S_FMT Failed: %s", __func__, strerror(errno));
        return ret;
    }

    if(mWidth == 2592 && mHeight == 1944)
      setOmitFrameCount(2);
    else
      setOmitFrameCount(0);

    return 0;
}
