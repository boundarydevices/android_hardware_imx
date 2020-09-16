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
#define LOG_TAG "CameraDeviceHwlImpl"

#include <string.h>
#include <linux/videodev2.h>
#include <log/log.h>
#include <hardware/camera_common.h>
#include "CameraDeviceHWLImpl.h"
#include "CameraDeviceSessionHWLImpl.h"

namespace android {

std::unique_ptr<CameraDeviceHwl> CameraDeviceHwlImpl::Create(
    uint32_t camera_id, const char *devPath,
    CscHw cam_copy_hw, CscHw cam_csc_hw, const char *hw_jpeg, CameraSensorMetadata *cam_metadata)
{
    ALOGI("%s: id %d, path %s, copy hw %d, csc hw %d, hw_jpeg %s",
      __func__, camera_id, devPath, cam_copy_hw, cam_csc_hw, hw_jpeg);

    auto device = std::unique_ptr<CameraDeviceHwlImpl>(
        new CameraDeviceHwlImpl(camera_id, devPath, cam_copy_hw, cam_csc_hw, hw_jpeg, cam_metadata));

    if (device == nullptr) {
        ALOGE("%s: Creating CameraDeviceHwlImpl failed.", __func__);
        return nullptr;
    }

    status_t res = device->Initialize();
    if (res != OK) {
        ALOGE("%s: Initializing CameraDeviceHwlImpl failed: %s (%d).",
              __func__,
              strerror(-res),
              res);
        return nullptr;
    }

    ALOGI("%s: Created CameraDeviceHwlImpl for camera %u", __func__, device->camera_id_);

    return device;
}

CameraDeviceHwlImpl::CameraDeviceHwlImpl(
    uint32_t camera_id, const char *devPath,
    CscHw cam_copy_hw, CscHw cam_csc_hw, const char *hw_jpeg, CameraSensorMetadata *cam_metadata)
    : camera_id_(camera_id),
      mCamBlitCopyType(cam_copy_hw),
      mCamBlitCscType(cam_csc_hw)
{
    strncpy(mDevPath, devPath, CAMAERA_FILENAME_LENGTH);
    mDevPath[CAMAERA_FILENAME_LENGTH - 1] = 0;

    strncpy(mJpegHw, hw_jpeg, JPEG_HW_NAME_LEN);
    mJpegHw[JPEG_HW_NAME_LEN-1] = 0;

    memcpy(&mSensorData, cam_metadata, sizeof(CameraSensorMetadata));

    memset(mSensorFormats, 0, sizeof(mSensorFormats));
    memset(mAvailableFormats, 0, sizeof(mAvailableFormats));

    memset(mPreviewResolutions, 0, sizeof(mPreviewResolutions));
    memset(mPictureResolutions, 0, sizeof(mPictureResolutions));
}

CameraDeviceHwlImpl::~CameraDeviceHwlImpl()
{
    if(m_meta)
       delete(m_meta);
}

status_t CameraDeviceHwlImpl::Initialize()
{
    ALOGI("%s", __func__);

    int ret = initSensorStaticData();
    if(ret) {
        ALOGE("%s: initSensorStaticData failed, ret %d", __func__, ret);
        return ret;
    }

    m_meta = new CameraMetadata();
    m_meta->createMetadata(this);
    m_meta->setTemplate();

    return OK;
}

status_t CameraDeviceHwlImpl::initSensorStaticData()
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


status_t CameraDeviceHwlImpl::adjustPreviewResolutions()
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

status_t CameraDeviceHwlImpl::setMaxPictureResolutions()
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

status_t CameraDeviceHwlImpl::GetCameraCharacteristics(
    std::unique_ptr<HalCameraMetadata>* characteristics) const
{
    if (characteristics == nullptr) {
        return BAD_VALUE;
    }

    *characteristics = HalCameraMetadata::Clone(m_meta->GetStaticMeta());

    return OK;
}

status_t CameraDeviceHwlImpl::DumpState(int /*fd*/)
{
    return OK;
}

status_t CameraDeviceHwlImpl::CreateCameraDeviceSessionHwl(
    CameraBufferAllocatorHwl* /*camera_allocator_hwl*/,
    std::unique_ptr<CameraDeviceSessionHwl>* session)
{
    if (session == nullptr) {
        ALOGE("%s: session is nullptr.", __func__);
        return BAD_VALUE;
    }

    CameraMetadata *pMeta = m_meta->Clone();

    *session = CameraDeviceSessionHwlImpl::Create(
        camera_id_, pMeta, this);

    if (*session == nullptr) {
        ALOGE("%s: Cannot create CameraDeviceSessionHWlImpl.", __func__);
        return BAD_VALUE;
    }

    return OK;
}

}  // namespace android
