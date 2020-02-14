/*
 *  Copyright 2019 NXP.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "ImxCamera.h"

ImxCamera::ImxCamera(int32_t id, int32_t facing, int32_t orientation, char *path, CscHw cam_copy_hw,
                                                CscHw cam_csc_hw, CameraSensorMetadata *cam_metadata)
   : Camera(id, facing, orientation, path, cam_copy_hw, cam_csc_hw)
{
    mCameraMetadata = cam_metadata;

    if (cam_metadata->buffer_type == CameraSensorMetadata::kMmap)
        mVideoStream = new ImxCameraMMAPStream(this, cam_metadata->omit_frame);
    else if (cam_metadata->buffer_type == CameraSensorMetadata::kDma)
        mVideoStream = new ImxCameraDMAStream(this, cam_metadata->omit_frame);
}

ImxCamera::~ImxCamera()
{
}


int ImxCamera::ImxCameraMMAPStream::getCaptureMode(int width, int height)
{
    int index = 0;
    int ret = 0;
    int capturemode = 0;
    struct v4l2_frmsizeenum cam_frmsize;

    while (ret == 0) {
        cam_frmsize.index = index++;
        cam_frmsize.pixel_format = v4l2_fourcc('Y', 'U', 'Y', 'V');
        ret = ioctl(mDev, VIDIOC_ENUM_FRAMESIZES, &cam_frmsize);
        if ((cam_frmsize.discrete.width == (uint32_t)width) && (cam_frmsize.discrete.height == (uint32_t)height)
            && (ret == 0)) {
            capturemode = cam_frmsize.index;
            break;
        }
    }

    return capturemode;
}

status_t ImxCamera::initSensorStaticData()
{
    int32_t fd = open(mDevPath, O_RDWR);
    if (fd < 0) {
        ALOGE("ImxCameraCameraDevice: initParameters sensor has not been opened");
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
    int previewCnt = 0, pictureCnt = 0;
    struct v4l2_frmsizeenum cam_frmsize;
    struct v4l2_frmivalenum vid_frmval;
    while (ret == 0) {
        memset(TmpStr, 0, 20);
        memset(&cam_frmsize, 0, sizeof(struct v4l2_frmsizeenum));
        cam_frmsize.index = index++;
        cam_frmsize.pixel_format =
            convertPixelFormatToV4L2Format(mSensorFormats[0]);
        ret = ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &cam_frmsize);
        if (ret != 0) {
            continue;
        }
        ALOGI("enum frame size w:%d, h:%d", cam_frmsize.discrete.width, cam_frmsize.discrete.height);

        if (cam_frmsize.discrete.width == 0 ||
              cam_frmsize.discrete.height == 0) {
            continue;
        }

        vid_frmval.index = 0;
        vid_frmval.pixel_format = cam_frmsize.pixel_format;
        vid_frmval.width = cam_frmsize.discrete.width;
        vid_frmval.height = cam_frmsize.discrete.height;

        ret = ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &vid_frmval);
        if (ret != 0) {
            continue;
        }


        // If w/h ratio is not same with senserW/sensorH, framework assume that
        // first crop little width or little height, then scale.
        // 176x144 not work in this mode.

        if (!(cam_frmsize.discrete.width == 176 &&
              cam_frmsize.discrete.height == 144) &&
              vid_frmval.discrete.denominator /  vid_frmval.discrete.numerator >= 5) {
            mPictureResolutions[pictureCnt++] = cam_frmsize.discrete.width;
            mPictureResolutions[pictureCnt++] = cam_frmsize.discrete.height;
        }

        if (vid_frmval.discrete.denominator /  vid_frmval.discrete.numerator >= 15) {
            mPreviewResolutions[previewCnt++] = cam_frmsize.discrete.width;
            mPreviewResolutions[previewCnt++] = cam_frmsize.discrete.height;
        }
    }  // end while

    mPreviewResolutionCount = previewCnt;
    mPictureResolutionCount = pictureCnt;

    mMinFrameDuration = mCameraMetadata->minframeduration;
    mMaxFrameDuration = mCameraMetadata->maxframeduration;
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

    mFocalLength = mCameraMetadata->focallength;
    mPhysicalWidth = mCameraMetadata->physicalwidth;
    mPhysicalHeight = mCameraMetadata->physicalheight;
    mActiveArrayWidth = mCameraMetadata->activearraywidth;
    mActiveArrayHeight = mCameraMetadata->activearrayheight;
    mPixelArrayWidth = mCameraMetadata->pixelarraywidth;
    mPixelArrayHeight = mCameraMetadata->pixelarrayheight;
    mMaxJpegSize = mCameraMetadata->maxjpegsize;

    ALOGI("ImxdpuCsi, mFocalLength:%f, mPhysicalWidth:%f, mPhysicalHeight %f",
          mFocalLength,
          mPhysicalWidth,
          mPhysicalHeight);

    close(fd);
    return NO_ERROR;
}

PixelFormat ImxCamera::getPreviewPixelFormat()
{
    ALOGI("%s", __func__);
    return HAL_PIXEL_FORMAT_YCbCr_422_I;
}

// configure device.
int32_t ImxCamera::ImxCameraMMAPStream::onDeviceConfigureLocked()
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
    param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
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

    fmt.type                 = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width        = mWidth & 0xFFFFFFF8;
    fmt.fmt.pix.height       = mHeight & 0xFFFFFFF8;
    fmt.fmt.pix.pixelformat  = vformat;
    fmt.fmt.pix.priv         = 0;
    fmt.fmt.pix.sizeimage    = 0;
    fmt.fmt.pix.bytesperline = 0;

    ret = ioctl(mDev, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_S_FMT Failed: %s", __func__, strerror(errno));
        return ret;
    }

    setOmitFrameCount(0);

    struct OmitFrame *item;
    for(item = mOmitFrame; item < mOmitFrame + OMIT_RESOLUTION_NUM; item++) {
      if ((mWidth == item->width) && (mHeight == item->height)) {
        setOmitFrameCount(item->omitnum);
        break;
      }
    }

    return 0;
}

// configure device.
int32_t ImxCamera::ImxCameraDMAStream::onDeviceConfigureLocked()
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
    param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
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
    fmt.fmt.pix_mp.num_planes = 1;  /*ov5640 use YUYV format, is packed storage mode, set num_planes 1*/

    ret = ioctl(mDev, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_S_FMT Failed: %s", __func__, strerror(errno));
        return ret;
    }

    setOmitFrameCount(0);

    struct OmitFrame *item;
    for(item = mOmitFrame; item < mOmitFrame + OMIT_RESOLUTION_NUM; item++) {
      if ((mWidth == item->width) && (mHeight == item->height)) {
        setOmitFrameCount(item->omitnum);
        break;
      }
    }

    return 0;
}

int ImxCamera::ImxCameraDMAStream::getCaptureMode(int width, int height)
{
    int index = 0;
    int ret = 0;
    int capturemode = 0;
    struct v4l2_frmsizeenum cam_frmsize;

    while (ret == 0) {
        cam_frmsize.index = index++;
        cam_frmsize.pixel_format = v4l2_fourcc('Y', 'U', 'Y', 'V');
        ret = ioctl(mDev, VIDIOC_ENUM_FRAMESIZES, &cam_frmsize);
        if ((cam_frmsize.discrete.width == (uint32_t)width) && (cam_frmsize.discrete.height == (uint32_t)height)
            && (ret == 0)) {
            capturemode = cam_frmsize.index;
            break;
        }
    }

    return capturemode;
}
