/*
 *  Copyright 2020 NXP.
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

#include "ISPCamera.h"

ISPCamera::ISPCamera(int32_t id, int32_t facing, int32_t orientation, char *path, CscHw cam_copy_hw,
                     CscHw cam_csc_hw, const char *hw_jpeg_enc, CameraSensorMetadata *cam_metadata)
      : Camera(id, facing, orientation, path, cam_copy_hw, cam_csc_hw, hw_jpeg_enc) {
    ALOGI("create ISP Camera");
    mCameraMetadata = cam_metadata;

    if (cam_metadata->buffer_type != CameraSensorMetadata::kMmap) {
        ALOGW("ISPCamera only support mmap buffer type, change tyep %d to kMmap",
              cam_metadata->buffer_type);
        cam_metadata->buffer_type = CameraSensorMetadata::kMmap;
    }

    mCameraMetadata = cam_metadata;

    mVideoStream = new ISPCameraMMAPStream(this, cam_metadata->omit_frame);
}

ISPCamera::~ISPCamera() {}

status_t ISPCamera::initSensorStaticData() {
    int32_t fd = open(mDevPath, O_RDWR);
    if (fd < 0) {
        ALOGE("ISPCameraCameraDevice: initParameters sensor has not been opened");
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
    mSensorFormatCount = changeSensorFormats(sensorFormats, mSensorFormats, index);
    if (mSensorFormatCount == 0) {
        ALOGE("%s no sensor format enum", __func__);
        close(fd);
        return BAD_VALUE;
    }

    availFormats[index++] = v4l2_fourcc('N', 'V', '2', '1');
    mAvailableFormatCount = changeSensorFormats(availFormats, mAvailableFormats, index);

    index = 0;
    char TmpStr[20];
    int previewCnt = 0, pictureCnt = 0;

    // Currently, VIDIOC_ENUM_FRAMESIZES, VIDIOC_ENUM_FRAMEINTERVALS are no supported.
    // Resolutions if configed in json file. Fps is changed by isp commands.
    // Hard code them. Will refine in the future.
    mPictureResolutions[0] = 1920;
    mPictureResolutions[1] = 1080;
    mPreviewResolutions[0] = 1920;
    mPreviewResolutions[1] = 1080;

    pictureCnt = 2;
    previewCnt = 2;

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

    ALOGI("ISP Camera, mFocalLength:%f, mPhysicalWidth:%f, mPhysicalHeight %f", mFocalLength,
          mPhysicalWidth, mPhysicalHeight);

    close(fd);
    return NO_ERROR;
}

PixelFormat ISPCamera::getPreviewPixelFormat() {
    ALOGI("%s", __func__);
    return HAL_PIXEL_FORMAT_YCbCr_422_I;
}

// configure device.
int32_t ISPCamera::ISPCameraMMAPStream::onDeviceConfigureLocked() {
    ALOGI("%s", __func__);
    int32_t ret = 0;
    if (mDev <= 0) {
        ALOGE("%s invalid fd handle", __func__);
        return BAD_VALUE;
    }

    uint32_t fps = mFps;
    int32_t vformat;
    vformat = convertPixelFormatToV4L2Format(mFormat);

    struct v4l2_frmivalenum frmival;
    frmival.index = 0;
    frmival.pixel_format = vformat;
    frmival.width = mWidth;
    frmival.height = mHeight;

    ALOGI("Width * Height %d x %d format %c%c%c%c, fps: %d, mFps %d", mWidth, mHeight,
          vformat & 0xFF, (vformat >> 8) & 0xFF, (vformat >> 16) & 0xFF, (vformat >> 24) & 0xFF,
          fps, mFps);

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = mWidth & 0xFFFFFFF8;
    fmt.fmt.pix.height = mHeight & 0xFFFFFFF8;
    fmt.fmt.pix.pixelformat = vformat;
    fmt.fmt.pix.priv = 0;
    fmt.fmt.pix.sizeimage = 0;
    fmt.fmt.pix.bytesperline = 0;

    ret = ioctl(mDev, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_S_FMT Failed: %s", __func__, strerror(errno));
        return ret;
    }

    setOmitFrameCount(0);

    struct OmitFrame *item;
    for (item = mOmitFrame; item < mOmitFrame + OMIT_RESOLUTION_NUM; item++) {
        if ((mWidth == item->width) && (mHeight == item->height)) {
            setOmitFrameCount(item->omitnum);
            break;
        }
    }

    return 0;
}
