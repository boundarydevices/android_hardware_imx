/*
 * Copyright (C) 2020 The Android Open Source Project
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

//#define LOG_NDEBUG 0
#define LOG_TAG "ISPCameraDeviceHwlImpl"

#include <string.h>
#include <linux/videodev2.h>
#include <log/log.h>
#include <hardware/camera_common.h>
#include "ISPCameraDeviceHWLImpl.h"
#include "CameraDeviceSessionHWLImpl.h"

namespace android {

status_t ISPCameraDeviceHwlImpl::initSensorStaticData()
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

    int totalWaitUs = 0;
query_frmsize:
    ret = 0;
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

        if (vid_frmval.discrete.denominator / vid_frmval.discrete.numerator >= 5) {
            mPictureResolutions[pictureCnt++] = cam_frmsize.discrete.width;
            mPictureResolutions[pictureCnt++] = cam_frmsize.discrete.height;
        }

        if (vid_frmval.discrete.denominator / vid_frmval.discrete.numerator >= 15) {
            mPreviewResolutions[previewCnt++] = cam_frmsize.discrete.width;
            mPreviewResolutions[previewCnt++] = cam_frmsize.discrete.height;
        }
    }  // end while

#define WAIT_ITVL 100000 // 100ms
#define WAIT_TIMEOUT 5000000 // 5s

    if(previewCnt == 0) {
        usleep(WAIT_ITVL);
        totalWaitUs += WAIT_ITVL;
        if(totalWaitUs > WAIT_TIMEOUT) {
            ALOGE("%s, wait isp media server ready for %d Us", __func__, WAIT_TIMEOUT);
            return BAD_VALUE;
        }

        ALOGI("%s, isp media server may not ready, retry after %d Us", __func__, WAIT_ITVL);
        goto query_frmsize;
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

    i = 0;
    mTargetFpsRange[i++] = 10;
    mTargetFpsRange[i++] = 30;
    mTargetFpsRange[i++] = 15;
    mTargetFpsRange[i++] = 30;
    mTargetFpsRange[i++] = 30;
    mTargetFpsRange[i++] = 30;

    setMaxPictureResolutions();
    ALOGI("mMaxWidth:%d, mMaxHeight:%d", mMaxWidth, mMaxHeight);

    close(fd);
    return NO_ERROR;
}

#define V4L2_CID_VIV_SENSOR_MODE (V4L2_CID_PRIVATE_BASE + 0x01)
int32_t ISPCameraMMAPStream::onDeviceConfigureLocked(uint32_t format, uint32_t width, uint32_t height, uint32_t fps)
{
    ALOGI("%s", __func__);
    int32_t ret = 0;
    if (mDev <= 0) {
        ALOGE("%s invalid fd handle", __func__);
        return BAD_VALUE;
    }

    int32_t vformat;
    vformat = convertPixelFormatToV4L2Format(format);

    ALOGI("%s, Width * Height %d x %d format %c%c%c%c, fps: %d", __func__, width, height,
        vformat & 0xFF, (vformat >> 8) & 0xFF, (vformat >> 16) & 0xFF, (vformat >> 24) & 0xFF, fps);

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));

    fmt.type                 = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width        = width & 0xFFFFFFF8;
    fmt.fmt.pix.height       = height & 0xFFFFFFF8;
    fmt.fmt.pix.pixelformat  = vformat;

    ret = ioctl(mDev, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_S_FMT Failed: %s", __func__, strerror(errno));
        return ret;
    }

    ret = postConfigure(format, width, height, fps);

    return ret;
}

int32_t ISPCameraMMAPStream::onDeviceStartLocked()
{
    int ret = MMAPStream::onDeviceStartLocked();

    // When restart stream (shift between picture and record mode, or shift between APK), need recover to awb,
    // or the image will blurry if previous mode is mwb.
    // awb/aec need to be set after stream on.
    m_IspWrapper->processAWB(ANDROID_CONTROL_AWB_MODE_AUTO, true);
    m_IspWrapper->processAeMode(ANDROID_CONTROL_AE_MODE_ON, true);

    return ret;
}


int32_t ISPCameraMMAPStream::createISPWrapper(char *pDevPath, CameraSensorMetadata *pSensorData)
{
    int ret = 0;

    m_IspWrapper = std::make_unique<ISPWrapper>(pSensorData);
    ret = m_IspWrapper->init(pDevPath);

    return ret;
}

int32_t ISPCameraMMAPStream::ISPProcess(void *pMeta)
{
    return m_IspWrapper->process((HalCameraMetadata *)pMeta);
}

}  // namespace android
