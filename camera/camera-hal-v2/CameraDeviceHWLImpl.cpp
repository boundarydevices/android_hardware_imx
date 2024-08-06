/*
 * Copyright (C) 2020 The Android Open Source Project
 * Copyright 2023-2024 NXP.
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
#define LOG_TAG "CameraDeviceHwlImpl"

#include "CameraDeviceHWLImpl.h"

#include <hardware/camera_common.h>
#include <linux/videodev2.h>
#include <log/log.h>
#include <string.h>

#include "CameraDeviceSessionHWLImpl.h"
namespace android {

std::unique_ptr<CameraDeviceHwl> CameraDeviceHwlImpl::Create(
        std::shared_ptr<libcamera::Camera> &camera, uint32_t camera_id, ImxEngine cam_copy_hw,
        ImxEngine cam_csc_hw, const char *hw_jpeg, int use_cpu_encoder,
        CameraSensorMetadata *cam_metadata, PhysicalDeviceMapPtr physical_devices,
        HwlCameraProviderCallback &callback) {
    ALOGI("%s: id %d, hw_jpeg %s, camera %p, %s", __func__, camera_id, hw_jpeg, camera.get(),
          camera->id().c_str());

    CameraDeviceHwlImpl *device = NULL;

    device = new CameraDeviceHwlImpl(camera_id, cam_copy_hw, cam_csc_hw, hw_jpeg, use_cpu_encoder,
                                     cam_metadata, std::move(physical_devices), callback);

    if (device == nullptr) {
        ALOGE("%s: Creating CameraDeviceHwlImpl failed.", __func__);
        return nullptr;
    }

    status_t res = device->Initialize(camera);
    if (res != OK) {
        ALOGE("%s: Initializing CameraDeviceHwlImpl failed: %s (%d).", __func__, strerror(-res),
              res);
        delete device;
        return nullptr;
    }

    ALOGI("%s: Created CameraDeviceHwlImpl for camera %u", __func__, device->camera_id_);

    return std::unique_ptr<CameraDeviceHwl>(device);
}

#define AP1302_95_NAME "/base/soc/bus@42000000/i2c@42530000/ap1302_mipi@3c"
CameraDeviceHwlImpl::CameraDeviceHwlImpl(uint32_t camera_id, ImxEngine cam_copy_hw,
                                         ImxEngine cam_csc_hw, const char *hw_jpeg,
                                         int use_cpu_encoder, CameraSensorMetadata *cam_metadata,
                                         PhysicalDeviceMapPtr physical_devices,
                                         HwlCameraProviderCallback &callback)
      : camera_id_(camera_id),
        mCallback(callback),
        mCamBlitCopyType(cam_copy_hw),
        mCamBlitCscType(cam_csc_hw),
        mUseCpuEncoder(use_cpu_encoder),
        physical_device_map_(std::move(physical_devices)) {
    strncpy(mJpegHw, hw_jpeg, JPEG_HW_NAME_LEN);
    mJpegHw[JPEG_HW_NAME_LEN - 1] = 0;

    memcpy(&mSensorData, cam_metadata, sizeof(CameraSensorMetadata));

    memset(mSensorFormats, 0, sizeof(mSensorFormats));
    memset(mAvailableFormats, 0, sizeof(mAvailableFormats));

    memset(mPreviewResolutions, 0, sizeof(mPreviewResolutions));
    memset(mPictureResolutions, 0, sizeof(mPictureResolutions));
}

CameraDeviceHwlImpl::~CameraDeviceHwlImpl() {
    if (m_meta) {
        delete m_meta;
        m_meta = NULL;
    }

    if (camera_) {
        camera_.reset();
    }
}

status_t CameraDeviceHwlImpl::Initialize(std::shared_ptr<libcamera::Camera> &camera) {
    ALOGI("%s", __func__);

    camera_ = camera;
    if (camera_ == nullptr) {
        ALOGE("%s:  cameraManager->get(%s) failed", __func__, AP1302_95_NAME);
        return BAD_VALUE;
    }

    // TODO use for IsStreamCombinationSupported
    int ret = initSensorStaticData();
    if (ret) {
        ALOGE("%s: initSensorStaticData failed, ret %d", __func__, ret);
        return ret;
    }

    m_meta = new CameraMetadata();
    m_meta->createMetadata(this, mSensorData);
    m_meta->setTemplate(mSensorData);

    return OK;
}

bool CameraDeviceHwlImpl::PickResByMetaData(int width, int height) {
    bool bPicked = true;

    if (mSensorData.mGivenResNum > 0) {
        bPicked = false;
        for (int i = 0; i < mSensorData.mGivenResNum; i++) {
            if ((mSensorData.mGivenRes[i].width == width) &&
                (mSensorData.mGivenRes[i].height == height)) {
                bPicked = true;
                break;
            }
        }
    }

    if (!bPicked)
        return false;

    if ((width >= mSensorData.mMinWidth) && (height >= mSensorData.mMinHeight) &&
        (width <= mSensorData.mMaxWidth) && (height <= mSensorData.mMaxHeight))
        return true;

    return false;
}

status_t CameraDeviceHwlImpl::initSensorStaticData() {
    // first read sensor format.
    int index = 0;
    int sensorFormats[MAX_SENSOR_FORMAT];
    int availFormats[MAX_SENSOR_FORMAT];
    memset(sensorFormats, 0, sizeof(sensorFormats));
    memset(availFormats, 0, sizeof(availFormats));

    // Don't support enum format, now hard code here.
    if (strcmp(mSensorData.v4l2_format, "nv12") == 0) {
        sensorFormats[index] = v4l2_fourcc('N', 'V', '1', '2');
        availFormats[index++] = v4l2_fourcc('N', 'V', '1', '2');
    } else {
        sensorFormats[index] = v4l2_fourcc('Y', 'U', 'Y', 'V');
        availFormats[index++] = v4l2_fourcc('Y', 'U', 'Y', 'V');
    }

    mSensorFormatCount = changeSensorFormats(sensorFormats, mSensorFormats, index);
    if (mSensorFormatCount == 0) {
        ALOGE("%s no sensor format enum", __func__);
        return BAD_VALUE;
    }
    availFormats[index++] = v4l2_fourcc('N', 'V', '2', '1');
    mAvailableFormatCount = changeSensorFormats(availFormats, mAvailableFormats, index);

    int resCandidatePreview_os08a20[] = {320, 240, 640, 480, 1280, 720, 1920, 1080};
    int resCandidatePicture_os08a20[] = {320, 240, 640, 480, 1280, 720, 1920, 1080};

    int resCandidatePreview_ap1302[] = {320, 240, 640, 480, 1280, 720, 1280, 800};
    int resCandidatePicture_ap1302[] = {320, 240, 640, 480, 1280, 720, 1280, 800};

    if (strstr(mSensorData.camera_name, "os08a20")) {
        mPreviewResolutionCount = ARRAY_SIZE(resCandidatePreview_os08a20) < MAX_RESOLUTION_SIZE
                ? ARRAY_SIZE(resCandidatePreview_os08a20)
                : MAX_RESOLUTION_SIZE;
        memcpy(mPreviewResolutions, resCandidatePreview_os08a20,
               mPreviewResolutionCount * sizeof(int));

        mPictureResolutionCount = ARRAY_SIZE(resCandidatePicture_os08a20) < MAX_RESOLUTION_SIZE
                ? ARRAY_SIZE(resCandidatePicture_os08a20)
                : MAX_RESOLUTION_SIZE;
        memcpy(mPictureResolutions, resCandidatePicture_os08a20,
               mPictureResolutionCount * sizeof(int));
    } else {
        mPreviewResolutionCount = ARRAY_SIZE(resCandidatePreview_ap1302) < MAX_RESOLUTION_SIZE
                ? ARRAY_SIZE(resCandidatePreview_ap1302)
                : MAX_RESOLUTION_SIZE;
        memcpy(mPreviewResolutions, resCandidatePreview_ap1302,
               mPreviewResolutionCount * sizeof(int));

        mPictureResolutionCount = ARRAY_SIZE(resCandidatePicture_ap1302) < MAX_RESOLUTION_SIZE
                ? ARRAY_SIZE(resCandidatePicture_ap1302)
                : MAX_RESOLUTION_SIZE;
        memcpy(mPictureResolutions, resCandidatePicture_ap1302,
               mPictureResolutionCount * sizeof(int));
    }

    int i;
    for (i = 0; i < MAX_RESOLUTION_SIZE && i < mPictureResolutionCount; i += 2) {
        ALOGI("SupportedPictureSizes: %d x %d", mPictureResolutions[i], mPictureResolutions[i + 1]);
    }

    adjustPreviewResolutions();
    for (i = 0; i < MAX_RESOLUTION_SIZE && i < mPreviewResolutionCount; i += 2) {
        ALOGI("SupportedPreviewSizes: %d x %d", mPreviewResolutions[i], mPreviewResolutions[i + 1]);
    }

    int fpsRange_os08a20[] = {10, 30, 15, 30, 30, 30};
    int fpsRange_ap1302[] = {10, 30, 15, 30, 30, 30, 15, 60, 60, 60};

    if (strstr(mSensorData.camera_name, "os08a20")) {
        int rangeCount = ARRAY_SIZE(fpsRange_os08a20);
        mFpsRangeCount = rangeCount <= MAX_FPS_RANGE ? rangeCount : MAX_FPS_RANGE;
        memcpy(mTargetFpsRange, fpsRange_os08a20, mFpsRangeCount * sizeof(int));
    } else {
        int rangeCount = ARRAY_SIZE(fpsRange_ap1302);
        mFpsRangeCount = rangeCount <= MAX_FPS_RANGE ? rangeCount : MAX_FPS_RANGE;
        memcpy(mTargetFpsRange, fpsRange_ap1302, mFpsRangeCount * sizeof(int));
    }

    setMaxPictureResolutions();
    ALOGI("mMaxWidth:%d, mMaxHeight:%d", mMaxWidth, mMaxHeight);

    if ((mMaxWidth == 0) || (mMaxHeight == 0)) {
        ALOGI("%s: remove camera id %d due to max size is %dx%d", __func__, camera_id_, mMaxWidth,
              mMaxHeight);
        mCallback.camera_device_status_change(camera_id_, CameraDeviceStatus::kNotPresent);
    }

    return NO_ERROR;
}

status_t CameraDeviceHwlImpl::adjustPreviewResolutions() {
    // Make sure max size is in the first.
    int xTmp, yTmp, xMax, yMax, idx;
    idx = 0;
    xTmp = xMax = mPreviewResolutions[0];
    yTmp = yMax = mPreviewResolutions[1];
    for (int i = 0; i < MAX_RESOLUTION_SIZE; i += 2) {
        if (mPreviewResolutions[i] > xMax) {
            xMax = mPreviewResolutions[i];
            yMax = mPreviewResolutions[i + 1];
            idx = i;
        }
    }

    mPreviewResolutions[0] = xMax;
    mPreviewResolutions[1] = yMax;
    mPreviewResolutions[idx] = xTmp;
    mPreviewResolutions[idx + 1] = yTmp;

    // Sequence 1280x720 before 1024x768
    int idx_720p = -1;
    int idx_768p = -1;

    for (int i = 0; i < MAX_RESOLUTION_SIZE; i += 2) {
        if ((mPreviewResolutions[i] == 1280) && (mPreviewResolutions[i + 1] == 720))
            idx_720p = i;

        if ((mPreviewResolutions[i] == 1024) && (mPreviewResolutions[i + 1] == 768))
            idx_768p = i;
    }

    if ((idx_720p > 0) && (idx_768p > 0) && (idx_720p > idx_768p)) {
        mPreviewResolutions[idx_768p] = 1280;
        mPreviewResolutions[idx_768p + 1] = 720;
        mPreviewResolutions[idx_720p] = 1024;
        mPreviewResolutions[idx_720p + 1] = 768;
    }

    return 0;
}

status_t CameraDeviceHwlImpl::setMaxPictureResolutions() {
    int xMax, yMax;
    xMax = mPictureResolutions[0];
    yMax = mPictureResolutions[1];

    for (int i = 0; i < MAX_RESOLUTION_SIZE; i += 2) {
        if (mPictureResolutions[i] > xMax || mPictureResolutions[i + 1] > yMax) {
            xMax = mPictureResolutions[i];
            yMax = mPictureResolutions[i + 1];
        }
    }

    mMaxWidth = xMax;
    mMaxHeight = yMax;

    return 0;
}

bool HasCapability(const HalCameraMetadata *metadata, uint8_t capability) {
    if (metadata == nullptr) {
        return false;
    }

    camera_metadata_ro_entry_t entry;
    auto ret = metadata->Get(ANDROID_REQUEST_AVAILABLE_CAPABILITIES, &entry);
    if (ret != OK) {
        return false;
    }
    for (size_t i = 0; i < entry.count; i++) {
        if (entry.data.u8[i] == capability) {
            return true;
        }
    }
    return false;
}

status_t CameraDeviceHwlImpl::GetCameraCharacteristics(
        std::unique_ptr<HalCameraMetadata> *characteristics) const {
    if (characteristics == nullptr) {
        return BAD_VALUE;
    }

    *characteristics = HalCameraMetadata::Clone(m_meta->GetStaticMeta());

    return OK;
}

status_t CameraDeviceHwlImpl::GetPhysicalCameraCharacteristics(
        uint32_t physical_camera_id, std::unique_ptr<HalCameraMetadata> *characteristics) const {
    if (characteristics == nullptr) {
        return BAD_VALUE;
    }

    if (physical_device_map_.get() == nullptr) {
        ALOGE("%s: Camera %d is not a logical device!", __func__, camera_id_);
        return NO_INIT;
    }

    if (physical_device_map_->find(physical_camera_id) == physical_device_map_->end()) {
        ALOGE("%s: Physical camera id %d is not part of logical camera %d!", __func__,
              physical_camera_id, camera_id_);
        return BAD_VALUE;
    }

    *characteristics = HalCameraMetadata::Clone((physical_meta_map_.at(physical_camera_id)).get());

    return OK;
}

status_t CameraDeviceHwlImpl::DumpState(int /*fd*/) {
    return OK;
}

status_t CameraDeviceHwlImpl::CreateCameraDeviceSessionHwl(
        CameraBufferAllocatorHwl * /*camera_allocator_hwl*/,
        std::unique_ptr<CameraDeviceSessionHwl> *session) {
    if (session == nullptr) {
        ALOGE("%s: session is nullptr.", __func__);
        return BAD_VALUE;
    }

    HalCameraMetadata *cam_meta = m_meta->GetStaticMeta();
    std::unique_ptr<HalCameraMetadata> pMeta = HalCameraMetadata::Clone(cam_meta);

    auto physical_meta_map_ptr = std::make_unique<PhysicalMetaMap>();
    for (const auto &it : physical_meta_map_) {
        physical_meta_map_ptr->emplace(it.first, HalCameraMetadata::Clone(it.second.get()));
    }

    *session = CameraDeviceSessionHwlImpl::Create(camera_id_, std::move(pMeta), this,
                                                  ClonePhysicalDeviceMap(physical_meta_map_ptr));

    if (*session == nullptr) {
        ALOGE("%s: Cannot create CameraDeviceSessionHWlImpl.", __func__);
        return BAD_VALUE;
    }

    return OK;
}

bool CameraDeviceHwlImpl::FoundResoulution(int width, int height, int *resArray, int size) {
    if (resArray == NULL)
        return false;

    for (int i = 0; i < size; i += 2) {
        if ((width == resArray[i]) && (height == resArray[i + 1]))
            return true;
    }

    return false;
}

bool CameraDeviceHwlImpl::IsStreamCombinationSupported(const StreamConfiguration &stream_config) {
    return StreamCombJudge(stream_config, mPreviewResolutions, mPreviewResolutionCount,
                           mPictureResolutions, mPictureResolutionCount);
}

bool CameraDeviceHwlImpl::StreamCombJudge(const StreamConfiguration &stream_config,
                                          int *pPreviewResolutions, int nPreviewResolutionCount,
                                          int *pPictureResolutions, int nPictureResolutionCount) {
    for (const auto &stream : stream_config.streams) {
        if (stream.stream_type != google_camera_hal::StreamType::kOutput) {
            ALOGE("%s: only support stream type output, but it's %d", __func__,
                  (int)stream.stream_type);
            return false;
        }

        if (stream.format == -1) {
            ALOGE("%s: invalid format -1", __func__);
            return false;
        }

        bool bFound;
        if (stream.format == HAL_PIXEL_FORMAT_BLOB)
            bFound = FoundResoulution(stream.width, stream.height, pPictureResolutions,
                                      nPictureResolutionCount);
        else
            bFound = FoundResoulution(stream.width, stream.height, pPreviewResolutions,
                                      nPreviewResolutionCount);

        if (bFound == false) {
            ALOGE("%s: not support format 0x%x, resolution %dx%d", __func__, stream.format,
                  stream.width, stream.height);
            return false;
        }
    }

    return true;
}

status_t CameraDeviceHwlImpl::SetTorchMode(TorchMode mode __unused) {
    return INVALID_OPERATION;
}

status_t CameraDeviceHwlImpl::ConstructDefaultRequestSettings(
        RequestTemplate type, std::unique_ptr<HalCameraMetadata> *default_settings) {
    return m_meta->getRequestSettings(type, default_settings);
}

} // namespace android
