/*
 * Copyright (C) 2020 The Android Open Source Project
 * Copyright 2023 NXP.
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

// #define LOG_NDEBUG 0
#define LOG_TAG "ISPCameraDeviceHwlImpl"

#include "ISPCameraDeviceHWLImpl.h"

#include <hardware/camera_common.h>
#include <linux/videodev2.h>
#include <log/log.h>
#include <string.h>

#include "CameraDeviceSessionHWLImpl.h"

namespace android {

static int32_t GetRawFormat(int fd, int32_t &raw_v4l2_format, int8_t &color_arrange);

#define PROP_CAMERA_LAYOUT "ro.boot.camera.layout"
status_t ISPCameraDeviceHwlImpl::initSensorStaticData() {
    int32_t fd = open(*mDevPath[0], O_RDWR);
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
    mSensorFormatCount = changeSensorFormats(sensorFormats, mSensorFormats, index);
    if (mSensorFormatCount == 0) {
        ALOGE("%s no sensor format enum", __func__);
        close(fd);
        return BAD_VALUE;
    }

    availFormats[index++] = v4l2_fourcc('N', 'V', '2', '1');
    mAvailableFormatCount = changeSensorFormats(availFormats, mAvailableFormats, index);

    ret = 0;
    index = 0;
    char TmpStr[20];
    int previewCnt = 0, pictureCnt = 0;
    struct v4l2_frmsizeenum cam_frmsize;

    memset(TmpStr, 0, 20);
    memset(&cam_frmsize, 0, sizeof(struct v4l2_frmsizeenum));
    cam_frmsize.index = index++;
    cam_frmsize.pixel_format = convertPixelFormatToV4L2Format(mSensorFormats[0]);
    cam_frmsize.type = V4L2_FRMSIZE_TYPE_STEPWISE;
    ret = ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &cam_frmsize);
    if (ret != 0) {
        ALOGE("%s VIDIOC_ENUM_FRAMESIZES failed, ret %d", __func__, ret);
        close(fd);
        return BAD_VALUE;
    }

    int w_min = cam_frmsize.stepwise.min_width;
    int w_max = cam_frmsize.stepwise.max_width;
    int w_step = cam_frmsize.stepwise.step_width;
    int h_min = cam_frmsize.stepwise.min_height;
    int h_max = cam_frmsize.stepwise.max_height;
    int h_step = cam_frmsize.stepwise.step_height;

    ALOGI("enum frame size, width: min %d, max %d, step %d, height: min %d, max %d, step %d", w_min,
          w_max, w_step, h_min, h_max, h_step);

    // Support resolutions requeted by CCD
    uint32_t ispResCandidate[] = {176, 144, 320, 240, 640, 480, 1280, 720, 1920, 1080, 3840, 2160};
    uint32_t ispResCandidateNum = ARRAY_SIZE(ispResCandidate) / 2;
    uint32_t ispRes[MAX_RESOLUTION_SIZE];
    uint32_t ispResNum = 0;

    // filt out candidate
    for (uint32_t i = 0; i < ispResCandidateNum; i++) {
        int width = ispResCandidate[i * 2];
        int height = ispResCandidate[i * 2 + 1];
        if ((width < w_min) || (width > w_max) || ((width - w_min) % w_step != 0) ||
            (height < h_min) || (height > h_max) || ((height - h_min) % h_step != 0)) {
            ALOGW("%s, filt out %dx%d", __func__, width, height);
            continue;
        }

        bool bPicked = PickResByMetaData(width, height);
        if (!bPicked) {
            ALOGI("%s: res %dx%d is not picked due to settings in config json", __func__, width,
                  height);
            continue;
        }

        if (ispResNum * 2 >= MAX_RESOLUTION_SIZE)
            break;

        ispRes[ispResNum * 2] = width;
        ispRes[ispResNum * 2 + 1] = height;
        ispResNum++;
    }

    for (uint32_t i = 0; i < ispResNum; i++) {
        int w = ispRes[i * 2];
        int h = ispRes[i * 2 + 1];

        if ((w != 176) || (h != 144)) {
            mPictureResolutions[pictureCnt++] = w;
            mPictureResolutions[pictureCnt++] = h;
        }

        mPreviewResolutions[previewCnt++] = w;
        mPreviewResolutions[previewCnt++] = h;
    }

    mPreviewResolutionCount = previewCnt;
    mPictureResolutionCount = pictureCnt;

    int i;
    for (i = 0; i < MAX_RESOLUTION_SIZE && i < pictureCnt; i += 2) {
        ALOGI("SupportedPictureSizes: %d x %d", mPictureResolutions[i], mPictureResolutions[i + 1]);
    }

    adjustPreviewResolutions();
    for (i = 0; i < MAX_RESOLUTION_SIZE && i < previewCnt; i += 2) {
        ALOGI("SupportedPreviewSizes: %d x %d", mPreviewResolutions[i], mPreviewResolutions[i + 1]);
    }

    char layout[PROPERTY_VALUE_MAX] = {0};
    property_get(PROP_CAMERA_LAYOUT, layout, "");

    int fpsRangeBasler[] = {15, 30, 30, 30, 15, 60, 60, 60};
    int fpsRangeOs08a20[] = {20, 20, 15, 30, 30, 30, 15, 60, 60, 60};

    if (strstr(layout, "os08a20")) {
        int rangeCount = ARRAY_SIZE(fpsRangeOs08a20);
        mFpsRangeCount = rangeCount <= MAX_FPS_RANGE ? rangeCount : MAX_FPS_RANGE;
        memcpy(mTargetFpsRange, fpsRangeOs08a20, mFpsRangeCount * sizeof(int));
    } else {
        int rangeCount = ARRAY_SIZE(fpsRangeBasler);
        mFpsRangeCount = rangeCount <= MAX_FPS_RANGE ? rangeCount : MAX_FPS_RANGE;
        memcpy(mTargetFpsRange, fpsRangeBasler, mFpsRangeCount * sizeof(int));
    }

    setMaxPictureResolutions();
    ALOGI("mMaxWidth:%d, mMaxHeight:%d", mMaxWidth, mMaxHeight);

    /* get caps */
    ret = ioctl(fd, VIV_VIDIOC_GET_CAPS_SUPPORTS, &caps_supports);
    if (ret) {
        ALOGE("%s: Get Caps Supports Failed, ret %d", __func__, ret);
        close(fd);
        return BAD_VALUE;
    }

    ALOGI("%s: caps supports:{", __func__);
    ALOGI("\tcount = %d", caps_supports.count);
    for (unsigned int i = 0; i < caps_supports.count; i++) {
        ALOGI("\t{");
        ALOGI("\tindex            = %d", caps_supports.mode[i].index);
        ALOGI("\tbounds_width     = %d", caps_supports.mode[i].bounds_width);
        ALOGI("\tbounds_height    = %d", caps_supports.mode[i].bounds_height);
        ALOGI("\ttop              = %d", caps_supports.mode[i].top);
        ALOGI("\tleft             = %d", caps_supports.mode[i].left);
        ALOGI("\twidth            = %d", caps_supports.mode[i].width);
        ALOGI("\theight           = %d", caps_supports.mode[i].height);
        ALOGI("\thdr_mode         = %d", caps_supports.mode[i].hdr_mode);
        ALOGI("\tstitching_mode   = %d", caps_supports.mode[i].stitching_mode);
        ALOGI("\tbit_width        = %d", caps_supports.mode[i].bit_width);
        ALOGI("\tbayer_pattern    = %d", caps_supports.mode[i].bayer_pattern);
        ALOGI("\tfps              = %d", caps_supports.mode[i].fps);
        ALOGI("\t}");
    }
    ALOGI("}");

    if (strstr(layout, "basler") && (caps_supports.count >= 4)) {
        ALOGI("%s: hard code basler mode 2, 3 to hdr mode", __func__);
        caps_supports.mode[2].hdr_mode = 1;
        caps_supports.mode[3].hdr_mode = 1;
    }

    // m_color_arrange is used for static meta ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT.
    // Luckly, for os0a20, 4 modes all use BGBG/GRGR.
    // for basler, 4 modes all use GRGR/BGBG.
    ret = GetRawFormat(fd, m_raw_v4l2_format, m_color_arrange);
    if (ret) {
        ALOGE("%s: GetRawFormat Failed, ret %d", __func__, ret);
        close(fd);
        return BAD_VALUE;
    }

    close(fd);
    return NO_ERROR;
}

static int32_t GetRawFormat(int fd, int32_t &raw_v4l2_format, int8_t &color_arrange) {
    if (fd < 0)
        return BAD_VALUE;

    v4l2_fmtdesc formatDescriptions;
    formatDescriptions.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (int i = 0; true; i++) {
        formatDescriptions.index = i;
        if (ioctl(fd, VIDIOC_ENUM_FMT, &formatDescriptions) != 0)
            break;

        ALOGI("format %2d: %s 0x%08X 0x%X", i, formatDescriptions.description,
              formatDescriptions.pixelformat, formatDescriptions.flags);

        /* bits */
        switch (formatDescriptions.pixelformat) {
            case V4L2_PIX_FMT_SBGGR10:
            case V4L2_PIX_FMT_SGBRG10:
            case V4L2_PIX_FMT_SGRBG10:
            case V4L2_PIX_FMT_SRGGB10:
            case V4L2_PIX_FMT_SBGGR12:
            case V4L2_PIX_FMT_SGBRG12:
            case V4L2_PIX_FMT_SGRBG12:
            case V4L2_PIX_FMT_SRGGB12:
            case V4L2_PIX_FMT_SBGGR16:
            case V4L2_PIX_FMT_SGBRG16:
            case V4L2_PIX_FMT_SGRBG16:
            case V4L2_PIX_FMT_SRGGB16:
                raw_v4l2_format = formatDescriptions.pixelformat;
                break;
            default:
                break;
        }

        /* color arrange */
        switch (formatDescriptions.pixelformat) {
            case V4L2_PIX_FMT_SBGGR10:
            case V4L2_PIX_FMT_SBGGR12:
            case V4L2_PIX_FMT_SBGGR16:
                color_arrange = ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_BGGR;
                break;
            case V4L2_PIX_FMT_SGBRG10:
            case V4L2_PIX_FMT_SGBRG12:
            case V4L2_PIX_FMT_SGBRG16:
                color_arrange = ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_GBRG;
                break;
            case V4L2_PIX_FMT_SGRBG10:
            case V4L2_PIX_FMT_SGRBG12:
            case V4L2_PIX_FMT_SGRBG16:
                color_arrange = ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_GRBG;
                break;
            case V4L2_PIX_FMT_SRGGB10:
            case V4L2_PIX_FMT_SRGGB12:
            case V4L2_PIX_FMT_SRGGB16:
                color_arrange = ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_RGGB;
                break;
            default:
                break;
        }
    }

    ALOGI("%s: raw_v4l2_format 0x%x, color_arrange %d", __func__, raw_v4l2_format, color_arrange);

    if ((raw_v4l2_format == -1) || (color_arrange == -1)) {
        return BAD_VALUE;
    }

    return 0;
}

int32_t ISPCameraMMAPStream::onPrepareLocked(uint32_t format, uint8_t sceneMode) {
    int ret = 0;
    int sensorMode = mSession->getCapsMode(sceneMode);

    struct viv_caps_mode_s caps_mode;
    memset(&caps_mode, 0, sizeof(caps_mode));
    caps_mode.mode = sensorMode;

    ret = ioctl(mDev, VIV_VIDIOC_S_CAPS_MODE, &caps_mode);
    if (ret) {
        ALOGE("%s: Set sensor mode[%d] Failed\n", __func__, caps_mode.mode);
        return BAD_VALUE;
    }

    // Since raw format may change with mode, need update after set mode.
    ret = GetRawFormat(mDev, mSession->m_raw_v4l2_format, mSession->m_color_arrange);
    if (ret) {
        ALOGE("%s: GetRawFormat Failed, ret %d", __func__, ret);
        return BAD_VALUE;
    }

    m_IspWrapper->init(mDev);

    // Before capture raw data, need first disable DWE.
    if (format == HAL_PIXEL_FORMAT_RAW16)
        ISPProcess(NULL, format);

    return 0;
}

int32_t ISPCameraMMAPStream::onDeviceConfigureLocked(uint32_t format, uint32_t width,
                                                     uint32_t height, uint32_t fps) {
    ALOGI("%s", __func__);
    int32_t ret = 0;
    if (mDev <= 0) {
        ALOGE("%s invalid fd handle", __func__);
        return BAD_VALUE;
    }

    int32_t vformat;
    if (format == HAL_PIXEL_FORMAT_RAW16)
        vformat = mSession->getRawV4l2Format();
    else
        vformat = convertPixelFormatToV4L2Format(format);

    ALOGI("%s, Width * Height %d x %d format %c%c%c%c, fps: %d", __func__, width, height,
          vformat & 0xFF, (vformat >> 8) & 0xFF, (vformat >> 16) & 0xFF, (vformat >> 24) & 0xFF,
          fps);

    struct v4l2_streamparm param;
    memset(&param, 0, sizeof(param));

    param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    param.parm.capture.timeperframe.numerator = 1;
    param.parm.capture.timeperframe.denominator = fps;
    ret = ioctl(mDev, VIDIOC_S_PARM, &param);
    if (ret < 0) {
        ALOGW("%s: VIDIOC_S_PARM Failed: %s, fps %d", __func__, strerror(errno), fps);
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width & 0xFFFFFFF8;
    fmt.fmt.pix.height = height & 0xFFFFFFF8;
    fmt.fmt.pix.pixelformat = vformat;

    ret = ioctl(mDev, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_S_FMT Failed: %s", __func__, strerror(errno));
        return ret;
    }

    ret = postConfigureLocked(format, width, height, fps, vformat);

    return ret;
}

int32_t ISPCameraMMAPStream::onDeviceStartLocked() {
    int ret = 0;

    // Get the default dwe para.
    Json::Value jRequest, jResponse;
    ret = m_IspWrapper->viv_private_ioctl(IF_DWE_G_PARAMS, jRequest, jResponse);
    if (ret == 0) {
        m_IspWrapper->parseDewarpParams(jResponse["dwe"]);
    } else {
        ALOGW("%s: IF_DWE_G_PARAMS failed, ret %d", __func__, ret);
    }

    // Get exposure gain boundary.
    m_IspWrapper->getExpGainBoundary();

    // When restart stream (shift between picture and record mode, or shift between APK), need
    // recover to awb, or the image will blurry if previous mode is mwb. awb/aec need to be set
    // after stream on.
    if (isPictureIntent()) {
        m_IspWrapper->recoverExpWB();
        m_IspWrapper->recoverFeatures();
    } else {
        m_IspWrapper->recoverExpWB();
        m_IspWrapper->processAWB(ANDROID_CONTROL_AWB_MODE_AUTO, true);
        m_IspWrapper->processAeMode(ANDROID_CONTROL_AE_MODE_ON, true);
    }

    ret = MMAPStream::onDeviceStartLocked();
    if (ret) {
        ALOGE("%s: MMAPStream::onDeviceStartLocked failed, ret %d", __func__, ret);
        return ret;
    }

    return ret;
}

int32_t ISPCameraMMAPStream::onDeviceStopLocked() {
    m_IspWrapper->getLatestExpWB();
    m_IspWrapper->getLatestFeatures();

    int ret = MMAPStream::onDeviceStopLocked();
    return ret;
}

int32_t ISPCameraMMAPStream::createISPWrapper(char *pDevPath __unused,
                                              CameraSensorMetadata *pSensorData) {
    int ret = 0;

    m_IspWrapper = std::make_unique<ISPWrapper>(pSensorData, (void *)this);

    return ret;
}

int32_t ISPCameraMMAPStream::ISPProcess(void *pMeta, uint32_t format) {
    return m_IspWrapper->process((HalCameraMetadata *)pMeta, format);
}

} // namespace android
