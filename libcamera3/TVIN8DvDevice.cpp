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

#include "TVIN8DvDevice.h"
#include "USPStream.h"

#define Align(len, align) (((uint32_t)len + (align)-1) / (align) * (align))

TVIN8DvDevice::TVIN8DvDevice(int32_t id, int32_t facing, int32_t orientation, char *path)
    : Camera(id, facing, orientation, path)
{
    mVideoStream = new TVin8DvStream(this);
    mResCount = 0;
    memset(mResMap, 0, sizeof(mResMap));
}

TVIN8DvDevice::~TVIN8DvDevice()
{
}

status_t TVIN8DvDevice::initSensorStaticData()
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
        maxWait--;
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
    // sensorFormats[index] = v4l2_fourcc('N', 'V', '1', '2');
    // availFormats[index++] = v4l2_fourcc('N', 'V', '1', '2');

    sensorFormats[index] = v4l2_fourcc('Y', 'U', 'Y', 'V');
    availFormats[index++] = v4l2_fourcc('Y', 'U', 'Y', 'V');

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

        ALOGV("enum frame size w:%d, h:%d", vid_frmsize.discrete.width, vid_frmsize.discrete.height);

        // to align with gralloc alignment
        // for YUYV/NV12, 16 for width, 4 for height
        mResMap[mResCount].v4l2Width = vid_frmsize.discrete.width;
        mResMap[mResCount].v4l2Height = vid_frmsize.discrete.height;
        mResMap[mResCount].streamWidth = Align(vid_frmsize.discrete.width, 16);
        mResMap[mResCount].streamHeight = Align(vid_frmsize.discrete.height, 4);

        ALOGI("idx %d, v4l2 res %dx%d map to stream res %dx%d", mResCount, mResMap[mResCount].v4l2Width, mResMap[mResCount].v4l2Height, mResMap[mResCount].streamWidth, mResMap[mResCount].streamHeight);

        // memset(&vid_frmval, 0, sizeof(struct v4l2_frmivalenum));
        // vid_frmval.index        = 0;
        // vid_frmval.pixel_format = vid_frmsize.pixel_format;
        // vid_frmval.width        = vid_frmsize.discrete.width;
        // vid_frmval.height       = vid_frmsize.discrete.height;

        // ret = ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &vid_frmval);
        // if (ret != 0) {
        //    continue;
        //}
        // ALOGV("vid_frmval denominator:%d, numeraton:%d",
        //        vid_frmval.discrete.denominator,
        //        vid_frmval.discrete.numerator);

        // v4l2 does not support VIDIOC_ENUM_FRAMEINTERVALS, now hard code here.
        if ((mResMap[mResCount].streamWidth > 1280) ||
            (mResMap[mResCount].streamHeight > 800)) {
            vid_frmval.discrete.denominator = 15;
            vid_frmval.discrete.numerator = 1;
        } else {
            vid_frmval.discrete.denominator = 30;
            vid_frmval.discrete.numerator = 1;
        }

        // If w/h ratio is not same with senserW/sensorH, framework assume that
        // first crop little width or little height, then scale.
        // But 1920x1080, 176x144 not work in this mode.
        // For 1M pixel, 720p sometimes may take green picture(5%), so not report
        // it,
        // use 1024x768 for 1M pixel
        // 1920x1080 1280x720 is required by CTS.
        if (!(mResMap[mResCount].streamWidth == 176 &&
              mResMap[mResCount].streamHeight == 144)) {
            mPictureResolutions[pictureCnt++] = mResMap[mResCount].streamWidth;
            mPictureResolutions[pictureCnt++] = mResMap[mResCount].streamHeight;
        }

        if (vid_frmval.discrete.denominator / vid_frmval.discrete.numerator > 15) {
            mPreviewResolutions[previewCnt++] = mResMap[mResCount].streamWidth;
            mPreviewResolutions[previewCnt++] = mResMap[mResCount].streamHeight;
            ;
        }

        mResCount++;
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
    ALOGI("FrameDuration is %lld, %lld", mMinFrameDuration, mMaxFrameDuration);

    i = 0;
    mTargetFpsRange[i++] = 10;
    mTargetFpsRange[i++] = 30;
    mTargetFpsRange[i++] = 30;
    mTargetFpsRange[i++] = 30;

    setMaxPictureResolutions();
    ALOGI("mMaxWidth:%d, mMaxHeight:%d", mMaxWidth, mMaxHeight);

    mFocalLength = 3.37f;
    mPhysicalWidth = 3.6288f;   // 2592 x 1.4u
    mPhysicalHeight = 2.7216f;  // 1944 x 1.4u
    mActiveArrayWidth = 704;
    mActiveArrayHeight = 244;
    mPixelArrayWidth = 704;
    mPixelArrayHeight = 244;

    ALOGI("tvin device, mFocalLength:%f, mPhysicalWidth:%f, mPhysicalHeight %f",
          mFocalLength,
          mPhysicalWidth,
          mPhysicalHeight);

    close(fd);
    return NO_ERROR;
}

PixelFormat TVIN8DvDevice::getPreviewPixelFormat()
{
    ALOGI("%s", __func__);
    return HAL_PIXEL_FORMAT_YCbCr_422_I;
}

int32_t TVIN8DvDevice::getV4l2Res(uint32_t streamWidth, uint32_t streamHeight, uint32_t *pV4l2Width, uint32_t *pV4l2Height)
{
    uint32_t i;

    if ((pV4l2Width == NULL) || (pV4l2Height == NULL)) {
        ALOGE("%s, para null", __func__);
        return BAD_VALUE;
    }

    for (i = 0; i < mResCount; i++) {
        if ((streamWidth == mResMap[i].streamWidth) &&
            (streamHeight == mResMap[i].streamHeight)) {
            *pV4l2Width = mResMap[i].v4l2Width;
            *pV4l2Height = mResMap[i].v4l2Height;
            break;
        }
    }

    if (i >= mResCount) {
        ALOGE("%s, no v4l2 res found for stream res %dx%d, mResCount %d", __func__, streamWidth, streamHeight, mResCount);
        return BAD_VALUE;
    }

    return NO_ERROR;
}

int32_t TVIN8DvDevice::allocTmpBuf(uint32_t size)
{
    mTmpBuf = (uint8_t *)malloc(size);
    if (mTmpBuf == NULL) {
        ALOGE("%s, mTmpBuf alloc failed, size %d", __func__, size);
        return BAD_VALUE;
    }

    ALOGI("%s, allocTmpBuf, size %d", __func__, size);

    return NO_ERROR;
}

void TVIN8DvDevice::freeTmpBuf()
{
    if (mTmpBuf) {
        free(mTmpBuf);
        mTmpBuf = NULL;
    }

    return;
}

// configure device.
int32_t TVIN8DvDevice::TVin8DvStream::onDeviceConfigureLocked()
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
        maxWait--;
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

    int32_t fps = mFps;
    int32_t vformat;
    vformat = convertPixelFormatToV4L2Format(mFormat);

    if ((mWidth > 1920) || (mHeight > 1080)) {
        fps = 15;
    }

    ALOGI("Width * Height %d x %d format %c%c%c%c, fps: %d", mWidth, mHeight, vformat & 0xFF, (vformat >> 8) & 0xFF, (vformat >> 16) & 0xFF, (vformat >> 24) & 0xFF, fps);

    struct v4l2_streamparm param;
    memset(&param, 0, sizeof(param));
    param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    param.parm.capture.timeperframe.numerator = 1;
    param.parm.capture.timeperframe.denominator = fps;
    param.parm.capture.capturemode = mCamera->getCaptureMode(mWidth, mHeight);
    ret = ioctl(mDev, VIDIOC_S_PARM, &param);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_S_PARM Failed: %s", __func__, strerror(errno));
        return ret;
    }

    uint32_t v4l2Width;
    uint32_t v4l2Height;

    ret = mCamera->getV4l2Res(mWidth, mHeight, &v4l2Width, &v4l2Height);
    if (ret) {
        ALOGE("%s getV4l2Res failed, ret %d", __func__, ret);
        return BAD_VALUE;
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = v4l2Width;
    fmt.fmt.pix.height = v4l2Height;
    fmt.fmt.pix.pixelformat = vformat;
    fmt.fmt.pix.priv = 0;
    fmt.fmt.pix.sizeimage = 0;
    fmt.fmt.pix.bytesperline = 0;

    // Special stride alignment for YU12
    if (vformat == v4l2_fourcc('Y', 'U', '1', '2')) {
        // Goolge define the the stride and c_stride for YUV420 format
        // y_size = stride * height
        // c_stride = ALIGN(stride/2, 16)
        // c_size = c_stride * height/2
        // size = y_size + c_size * 2
        // cr_offset = y_size
        // cb_offset = y_size + c_size
        // int stride = (width+15)/16*16;
        // int c_stride = (stride/2+16)/16*16;
        // y_size = stride * height
        // c_stride = ALIGN(stride/2, 16)
        // c_size = c_stride * height/2
        // size = y_size + c_size * 2
        // cr_offset = y_size
        // cb_offset = y_size + c_size

        // GPU and IPU take below stride calculation
        // GPU has the Y stride to be 32 alignment, and UV stride to be
        // 16 alignment.
        // IPU have the Y stride to be 2x of the UV stride alignment
        int32_t stride = (v4l2Width + 31) / 32 * 32;
        int32_t c_stride = (stride / 2 + 15) / 16 * 16;
        fmt.fmt.pix.bytesperline = stride;
        fmt.fmt.pix.sizeimage = stride * v4l2Height + c_stride * v4l2Height;
        ALOGI("Special handling for YV12 on Stride %d, size %d",
              fmt.fmt.pix.bytesperline,
              fmt.fmt.pix.sizeimage);
    }

    ret = ioctl(mDev, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_S_FMT Failed: %s", __func__, strerror(errno));
        return ret;
    }

    return 0;
}
