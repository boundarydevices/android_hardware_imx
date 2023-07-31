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
#include "ISPCameraDeviceHWLImpl.h"
#include "CameraDeviceSessionHWLImpl.h"
namespace android {

std::unique_ptr<CameraDeviceHwl> CameraDeviceHwlImpl::Create(
    uint32_t camera_id, std::vector<std::shared_ptr<char*>> devPaths, std::vector<uint32_t> physicalIds,
    CscHw cam_copy_hw, CscHw cam_csc_hw, const char *hw_jpeg, int use_cpu_encoder, CameraSensorMetadata *cam_metadata,
    PhysicalDeviceMapPtr physical_devices, HwlCameraProviderCallback &callback)
{
    ALOGI("%s: id %d, copy hw %d, csc hw %d, hw_jpeg %s",
        __func__, camera_id, cam_copy_hw, cam_csc_hw, hw_jpeg);

    CameraDeviceHwlImpl *device = NULL;

    if(strstr(cam_metadata->camera_name, ISP_SENSOR_NAME))
        device = new ISPCameraDeviceHwlImpl(camera_id, devPaths, physicalIds, cam_copy_hw, cam_csc_hw,
                            hw_jpeg, use_cpu_encoder, cam_metadata, std::move(physical_devices), callback);
    else
        device = new CameraDeviceHwlImpl(camera_id, devPaths, physicalIds, cam_copy_hw, cam_csc_hw,
                            hw_jpeg, use_cpu_encoder, cam_metadata, std::move(physical_devices), callback);

    if (device == nullptr) {
        ALOGE("%s: Creating CameraDeviceHwlImpl failed.", __func__);
        return nullptr;
    }

    status_t res = device->Initialize();
    if (res != OK) {
        ALOGE("%s: Initializing CameraDeviceHwlImpl failed: %s (%d).",
                __func__, strerror(-res), res);
        delete device;
        return nullptr;
    }

    ALOGI("%s: Created CameraDeviceHwlImpl for camera %u", __func__, device->camera_id_);

    return std::unique_ptr<CameraDeviceHwl>(device);
}

CameraDeviceHwlImpl::CameraDeviceHwlImpl(
    uint32_t camera_id, std::vector<std::shared_ptr<char*>> devPaths, std::vector<uint32_t> physicalIds,
    CscHw cam_copy_hw, CscHw cam_csc_hw, const char *hw_jpeg, int use_cpu_encoder, CameraSensorMetadata *cam_metadata,
    PhysicalDeviceMapPtr physical_devices, HwlCameraProviderCallback &callback)
    : camera_id_(camera_id),
        mCallback(callback),
        mCamBlitCopyType(cam_copy_hw),
        mCamBlitCscType(cam_csc_hw),
        mUseCpuEncoder(use_cpu_encoder),
        physical_device_map_(std::move(physical_devices))
{
    mDevPath = devPaths;
    for (int i = 0; i < (int)mDevPath.size(); ++i) {
        ALOGI("%s, mDevPath[%d] %s", __func__, i, *mDevPath[i]);
    }

    mPhysicalIds = physicalIds;
    for (int i = 0; i < (int)mPhysicalIds.size(); ++i) {
        ALOGI("%s, mPhysicalIds %u", __func__, mPhysicalIds[i]);
    }

    strncpy(mJpegHw, hw_jpeg, JPEG_HW_NAME_LEN);
    mJpegHw[JPEG_HW_NAME_LEN-1] = 0;

    memcpy(&mSensorData, cam_metadata, sizeof(CameraSensorMetadata));

    memset(mSensorFormats, 0, sizeof(mSensorFormats));
    memset(mAvailableFormats, 0, sizeof(mAvailableFormats));

    memset(mPreviewResolutions, 0, sizeof(mPreviewResolutions));
    memset(mPictureResolutions, 0, sizeof(mPictureResolutions));
    memset(&caps_supports, 0, sizeof(caps_supports));
    m_raw_v4l2_format = -1;
    m_color_arrange = -1;
}

CameraDeviceHwlImpl::~CameraDeviceHwlImpl()
{
    if(m_meta) {
        delete m_meta;
        m_meta = NULL;
    }
}

status_t CameraDeviceHwlImpl::Initialize()
{
    ALOGI("%s", __func__);

    //TODO use for IsStreamCombinationSupported
    int ret = initSensorStaticData();
    if(ret) {
        ALOGE("%s: initSensorStaticData failed, ret %d", __func__, ret);
        return ret;
    }

    //m_meta is logical or basic camera meta
    m_meta = new CameraMetadata();
    m_meta->createMetadata(this, mSensorData);
    m_meta->setTemplate(mSensorData);

    if (mSensorData.mAvailableCapabilities == ANDROID_REQUEST_AVAILABLE_CAPABILITIES_LOGICAL_MULTI_CAMERA) {
        uint8_t logical_available_capabilities[] = {ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE,
                                                ANDROID_REQUEST_AVAILABLE_CAPABILITIES_LOGICAL_MULTI_CAMERA};
        (m_meta->GetStaticMeta())->Set(ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                    logical_available_capabilities,
                    ARRAY_SIZE(logical_available_capabilities));

        uint8_t logical_sensor_sync_type = ANDROID_LOGICAL_MULTI_CAMERA_SENSOR_SYNC_TYPE_CALIBRATED;
        (m_meta->GetStaticMeta())->Set(ANDROID_LOGICAL_MULTI_CAMERA_SENSOR_SYNC_TYPE,
                    &logical_sensor_sync_type, 1);

        // Update 'android.logicalMultiCamera.physicalIds' according to the assigned physical ids.
        std::vector<uint8_t> physical_ids;
        camera_metadata_ro_entry_t entry;
        for (const auto &physical_device : *(physical_device_map_)) {
            auto physical_id = std::to_string(physical_device.first);
            physical_ids.insert(physical_ids.end(), physical_id.begin(),
                            physical_id.end());
            physical_ids.push_back('\0');

            //construct physical cameras' HalCameraMetadata for the logical camera
            CameraSensorMetadata *physical_chars = physical_device.second.second.get();
            CameraMetadata *m_phy_meta;
            m_phy_meta = new CameraMetadata();
            m_phy_meta->createMetadata(this, *physical_chars);
            m_phy_meta->setTemplate(*physical_chars);
            physical_meta_map_.emplace(
                physical_device.first, HalCameraMetadata::Clone(m_phy_meta->GetStaticMeta()));

            if(m_phy_meta) {
                delete m_phy_meta;
                m_phy_meta = nullptr;
            }
        }

        (m_meta->GetStaticMeta())->Set(ANDROID_LOGICAL_MULTI_CAMERA_PHYSICAL_IDS,
                    physical_ids.data(), physical_ids.size());

        // Additionally if possible try to emulate a logical camera device backed by
        // physical devices with different focal lengths.
        // Usually logical cameras will have device specific logic to switch between physical sensors.
        std::set<float> focal_lengths;
        for (const auto& it : physical_meta_map_) {
            auto ret = it.second->Get(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS, &entry);
            if ((ret == OK) && (entry.count > 0)) {
                focal_lengths.insert(entry.data.f, entry.data.f + entry.count);
            }
        }

        std::vector<float> focal_buffer;
        focal_buffer.reserve(focal_lengths.size());
        focal_buffer.insert(focal_buffer.end(), focal_lengths.begin(),
                                focal_lengths.end());
        (m_meta->GetStaticMeta())->Set(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
                        focal_buffer.data(), focal_buffer.size());

        auto ret = (m_meta->GetStaticMeta())->Get(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS, &entry);
        if (ret == OK) {
            std::set<int32_t> keys(entry.data.i32, entry.data.i32 + entry.count);
            keys.emplace(ANDROID_LENS_FOCAL_LENGTH);
            keys.emplace(ANDROID_LOGICAL_MULTI_CAMERA_ACTIVE_PHYSICAL_ID);
            std::vector<int32_t> keys_buffer(keys.begin(), keys.end());
            (m_meta->GetStaticMeta())->Set(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
                        keys_buffer.data(), keys_buffer.size());

            keys.clear();
            keys_buffer.clear();
            auto ret = (m_meta->GetStaticMeta())->Get(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS, &entry);
            if (ret == OK) {
                keys.insert(entry.data.i32, entry.data.i32 + entry.count);
                // Due to API limitations we currently don't support individual physical requests
                (m_meta->GetStaticMeta())->Erase(ANDROID_REQUEST_AVAILABLE_PHYSICAL_CAMERA_REQUEST_KEYS);
                keys.erase(ANDROID_REQUEST_AVAILABLE_PHYSICAL_CAMERA_REQUEST_KEYS);
                keys.emplace(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS);
                keys.emplace(ANDROID_LOGICAL_MULTI_CAMERA_PHYSICAL_IDS);
                keys_buffer.insert(keys_buffer.end(), keys.begin(), keys.end());
                (m_meta->GetStaticMeta())->Set(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
                            keys_buffer.data(), keys_buffer.size());
            }

            keys.clear();
            keys_buffer.clear();
            ret = (m_meta->GetStaticMeta())->Get(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, &entry);
            if (ret == OK) {
                keys.insert(entry.data.i32, entry.data.i32 + entry.count);
                keys.emplace(ANDROID_LENS_FOCAL_LENGTH);
                keys_buffer.insert(keys_buffer.end(), keys.begin(), keys.end());
                (m_meta->GetStaticMeta())->Set(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS,
                            keys_buffer.data(), keys_buffer.size());
            }
        }
    }

    return OK;
}

status_t CameraDeviceHwlImpl::initSensorStaticData()
{
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
    if (strcmp(mSensorData.v4l2_format, "nv12") == 0) {
        sensorFormats[index] = v4l2_fourcc('N', 'V', '1', '2');
        availFormats[index++] = v4l2_fourcc('N', 'V', '1', '2');
    } else {
        sensorFormats[index] = v4l2_fourcc('Y', 'U', 'Y', 'V');
        availFormats[index++] = v4l2_fourcc('Y', 'U', 'Y', 'V');
    }

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
    int maxFrmfps = 0;
    int frmfps = 0;
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
        ALOGI("enum frame size w:%d, h:%d, format 0x%x", cam_frmsize.discrete.width, cam_frmsize.discrete.height, cam_frmsize.pixel_format);

        if (cam_frmsize.discrete.width == 0 ||
            cam_frmsize.discrete.height == 0) {
            continue;
        }

        if (cam_frmsize.discrete.width <= 160 ||
            cam_frmsize.discrete.height <= 120) {
            continue;
        }

        vid_frmval.index = 0;
        vid_frmval.pixel_format = cam_frmsize.pixel_format;
        vid_frmval.width = cam_frmsize.discrete.width;
        vid_frmval.height = cam_frmsize.discrete.height;

        maxFrmfps = 0;
        while (ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &vid_frmval) == 0) {
            if (vid_frmval.discrete.numerator == 0)
                break;

            frmfps = vid_frmval.discrete.denominator /  vid_frmval.discrete.numerator;
            ALOGI("fps %d", frmfps);
            if (frmfps > maxFrmfps)
              maxFrmfps = frmfps;

            vid_frmval.index++;
        }
        ALOGI("maxFrmfps %d", maxFrmfps);

        // some camera did not support the VIDIOC_ENUM_FRAMEINTERVALS, such as ap1302
        if (maxFrmfps == 0)
            maxFrmfps = 30;

        // If w/h ratio is not same with senserW/sensorH, framework assume that
        // first crop little width or little height, then scale.
        // 176x144 not work in this mode.
        if (!(cam_frmsize.discrete.width == 176 &&
                cam_frmsize.discrete.height == 144) && maxFrmfps >= 5) {
            mPictureResolutions[pictureCnt++] = cam_frmsize.discrete.width;
            mPictureResolutions[pictureCnt++] = cam_frmsize.discrete.height;
        }

        if (maxFrmfps >= 15) {
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

    int fpsRange[] = {10, 30, 15, 30, 30, 30};
    int rangeCount = ARRAY_SIZE(fpsRange);
    mFpsRangeCount = rangeCount <= MAX_FPS_RANGE ? rangeCount : MAX_FPS_RANGE;
    memcpy(mTargetFpsRange, fpsRange, mFpsRangeCount*sizeof(int));

    setMaxPictureResolutions();
    ALOGI("mMaxWidth:%d, mMaxHeight:%d", mMaxWidth, mMaxHeight);

    close(fd);

    if ((mMaxWidth == 0) || (mMaxHeight == 0)) {
        ALOGI("%s: remove camera id %d due to max size is %dx%d", __func__, camera_id_, mMaxWidth, mMaxHeight);
        mCallback.camera_device_status_change(camera_id_, CameraDeviceStatus::kNotPresent);
    }

    return NO_ERROR;
}

status_t CameraDeviceHwlImpl::adjustPreviewResolutions()
{
    // Make sure max size is in the first.
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

    // Sequence 1280x720 before 1024x768
    int idx_720p = -1;
    int idx_768p = -1;

    for (int i=0; i<MAX_RESOLUTION_SIZE; i+=2) {
        if ((mPreviewResolutions[i] == 1280) && (mPreviewResolutions[i+1] == 720))
            idx_720p = i;

        if ((mPreviewResolutions[i] == 1024) && (mPreviewResolutions[i+1] == 768))
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
    std::unique_ptr<HalCameraMetadata>* characteristics) const
{
    if (characteristics == nullptr) {
        return BAD_VALUE;
    }

    HalCameraMetadata* cammeta = m_meta->GetStaticMeta();
    bool ret = HasCapability(cammeta, ANDROID_REQUEST_AVAILABLE_CAPABILITIES_LOGICAL_MULTI_CAMERA);
    if (ret == true)
        ALOGV("%s: This is a logical camera", __func__ );

    *characteristics = HalCameraMetadata::Clone(m_meta->GetStaticMeta());

    return OK;
}

status_t CameraDeviceHwlImpl::GetPhysicalCameraCharacteristics(
    uint32_t physical_camera_id,
    std::unique_ptr<HalCameraMetadata> *characteristics) const
{
    if (characteristics == nullptr) {
        return BAD_VALUE;
    }

    if (physical_device_map_.get() == nullptr) {
        ALOGE("%s: Camera %d is not a logical device!", __func__, camera_id_);
        return NO_INIT;
    }

    if (physical_device_map_->find(physical_camera_id) ==
        physical_device_map_->end()) {
        ALOGE("%s: Physical camera id %d is not part of logical camera %d!",
                __func__, physical_camera_id, camera_id_);
        return BAD_VALUE;
    }

    *characteristics = HalCameraMetadata::Clone((physical_meta_map_.at(physical_camera_id)).get());

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

    HalCameraMetadata* cam_meta = m_meta->GetStaticMeta();
    std::unique_ptr<HalCameraMetadata> pMeta =
        HalCameraMetadata::Clone(cam_meta);

    auto physical_meta_map_ptr = std::make_unique<PhysicalMetaMap>();
    for (const auto& it : physical_meta_map_) {
            physical_meta_map_ptr->emplace(it.first, HalCameraMetadata::Clone(it.second.get()));
        }

    *session = CameraDeviceSessionHwlImpl::Create(
        camera_id_,  std::move(pMeta), this, ClonePhysicalDeviceMap(physical_meta_map_ptr));

    if (*session == nullptr) {
        ALOGE("%s: Cannot create CameraDeviceSessionHWlImpl.", __func__);
        return BAD_VALUE;
    }

    return OK;
}

bool CameraDeviceHwlImpl::FoundResoulution(int width, int height, int *resArray, int size)
{
    if(resArray == NULL)
        return false;

    for (int i = 0; i < size; i += 2) {
        if ( (width == resArray[i]) && (height == resArray[i+1]) )
            return true;
    }

    return false;
}

bool CameraDeviceHwlImpl::IsStreamCombinationSupported(const StreamConfiguration& stream_config)
{
    return StreamCombJudge(stream_config,
                mPreviewResolutions, mPreviewResolutionCount,
                mPictureResolutions, mPictureResolutionCount);
}

bool CameraDeviceHwlImpl::StreamCombJudge(const StreamConfiguration& stream_config,
    int *pPreviewResolutions, int nPreviewResolutionCount, int *pPictureResolutions, int nPictureResolutionCount)
{
    for (const auto& stream : stream_config.streams) {
        if(stream.stream_type != google_camera_hal::StreamType::kOutput) {
            ALOGE("%s: only support stream type output, but it's %d", __func__, stream.stream_type);
            return false;
        }

        if(stream.format == -1) {
            ALOGE("%s: invalid format -1", __func__);
            return false;
        }

        bool bFound;
        if(stream.format == HAL_PIXEL_FORMAT_BLOB)
            bFound = FoundResoulution(stream.width, stream.height, pPictureResolutions, nPictureResolutionCount);
        else
            bFound = FoundResoulution(stream.width, stream.height, pPreviewResolutions, nPreviewResolutionCount);

        if (bFound == false) {
            ALOGE("%s: not support format 0x%x, resolution %dx%d", __func__, stream.format, stream.width, stream.height);
            return false;
        }
    }

    return true;
}

status_t CameraDeviceHwlImpl::SetTorchMode(TorchMode mode __unused)
{
    return INVALID_OPERATION;
}

}  // namespace android
