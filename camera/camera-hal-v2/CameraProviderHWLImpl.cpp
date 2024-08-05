/*
 *  Copyright 2020-2024 NXP.
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

// #define LOG_NDEBUG 0
#define LOG_TAG "CameraProviderHwlImpl"

#include "CameraProviderHWLImpl.h"

#include <android-base/file.h>
#include <android-base/strings.h>
#include <cutils/properties.h>
#include <hardware/camera_common.h>
#include <linux/videodev2.h>
#include <log/log.h>

#include "CameraDeviceHWLImpl.h"
#include "CameraDeviceSessionHWLImpl.h"
#include "CameraMetadata.h"
#include "ImageProcess.h"
#include "VendorTags.h"
#include "vendor_tag_defs.h"

namespace android {
std::unique_ptr<CameraProviderHwlImpl> CameraProviderHwlImpl::Create() {
    auto provider = std::unique_ptr<CameraProviderHwlImpl>(new CameraProviderHwlImpl());

    if (provider == nullptr) {
        ALOGE("%s: Creating CameraProviderHwlImpl failed.", __func__);
        return nullptr;
    }

    status_t res = provider->Initialize();
    if (res != OK) {
        ALOGE("%s: Initializing CameraProviderHwlImpl failed: %s (%d).", __func__, strerror(-res),
              res);
        return nullptr;
    }

    ALOGI("%s: Created CameraProviderHwlImpl", __func__);

    return provider;
}

status_t CameraProviderHwlImpl::Initialize() {
    ALOGI("enter %s", __func__);
    cameraManager_ = std::make_unique<libcamera::CameraManager>();

    cameraManager_->cameraAdded.connect(this, &CameraProviderHwlImpl::cameraAdded);
    cameraManager_->cameraRemoved.connect(this, &CameraProviderHwlImpl::cameraRemoved);

    int ret = cameraManager_->start();
    if (ret) {
        ALOGE("%s: Failed to start camera manager, ret %d", __func__, ret);
        cameraManager_.reset();
        return ret;
    }
    // check if camera exists.
    for (auto iter = mCameraDef.camera_id_map_.begin(); iter != mCameraDef.camera_id_map_.end();
         ++iter) {
        if (iter->second.size() >= 2) {
            // logical camera group
            bool logical_exist = true;
            for (int32_t phy_index = 0; phy_index < (int32_t)iter->second.size(); phy_index++) {
                logical_exist =
                        logical_exist && (mSets[(iter->second[phy_index]).second].mExisting);
            }

            if (logical_exist == true) {
                camera_id_maps.emplace(iter->first,
                                       std::vector<std::pair<CameraDeviceStatus, uint32_t>>());
                camera_id_maps[iter->first].reserve(iter->second.size());
                for (int32_t physical_index = 0; physical_index < (int32_t)iter->second.size();
                     physical_index++) {
                    int to_add_phy_cam_id = iter->second[physical_index].second;
                    auto device_status = CameraDeviceStatus::kPresent;
                    camera_id_maps[iter->first].push_back(
                            std::make_pair(device_status, to_add_phy_cam_id));
                }
            }
        } else {
            // basic camera
            if ((mSets[iter->first].mFacing != -1) && (mSets[iter->first].mExisting == true))
                camera_id_maps.emplace(iter->first,
                                       std::vector<std::pair<CameraDeviceStatus, uint32_t>>());
        }
    }

    mCameraCfgParser.Init();
    mCameraDef = mCameraCfgParser.mcamera();
    memset(&mCallback, 0, sizeof(mCallback));

    return OK;
}

// virtual ~CameraProviderHwlImpl();
CameraProviderHwlImpl::~CameraProviderHwlImpl() {
    fsl::ImageProcess* imageProcess = fsl::ImageProcess::getInstance();
    delete imageProcess;

    cameraManager_->stop();
    WaitForStatusCallbackFuture();
}

status_t CameraProviderHwlImpl::SetCallback(const HwlCameraProviderCallback& callback) {
    ALOGI("%s", __func__);
    physical_camera_status_cb_ = callback.physical_camera_device_status_change;

    mCallback = callback;
    return OK;
}

void CameraProviderHwlImpl::NotifyPhysicalCameraUnavailable() {
    for (const auto& one_map : camera_id_maps) {
        for (const auto& physical_device : one_map.second) {
            if (physical_device.first != CameraDeviceStatus::kNotPresent) {
                continue;
            }

            uint32_t logical_camera_id = one_map.first;
            uint32_t physical_camera_id = physical_device.second;
            physical_camera_status_cb_(logical_camera_id, physical_camera_id,
                                       CameraDeviceStatus::kNotPresent);
        }
    }
}

status_t CameraProviderHwlImpl::TriggerDeferredCallbacks() {
    std::lock_guard<std::mutex> lock(status_callback_future_lock_);
    if (status_callback_future_.valid()) {
        return OK;
    }

    status_callback_future_ =
            std::async(std::launch::async, &CameraProviderHwlImpl::NotifyPhysicalCameraUnavailable,
                       this);
    return OK;
}

void CameraProviderHwlImpl::WaitForStatusCallbackFuture() {
    std::lock_guard<std::mutex> lock(status_callback_future_lock_);
    if (!status_callback_future_.valid()) {
        // If there is no future pending, construct a dummy one.
        status_callback_future_ = std::async([]() { return; });
    }
    status_callback_future_.wait();
}

status_t CameraProviderHwlImpl::GetVendorTags(std::vector<VendorTagSection>* vendor_tag_sections) {
    if (vendor_tag_sections == nullptr) {
        ALOGE("%s: vendor_tag_sections is nullptr.", __func__);
        return BAD_VALUE;
    }

    vendor_tag_sections->assign(kImxTagSections.begin(), kImxTagSections.end());

    return OK;
}

status_t CameraProviderHwlImpl::GetVisibleCameraIds(std::vector<std::uint32_t>* camera_ids) {
    if (camera_ids == nullptr) {
        ALOGE("%s: camera_ids is nullptr.", __func__);
        return BAD_VALUE;
    }

    camera_ids->push_back(0);

    return OK;
}

status_t CameraProviderHwlImpl::CreateCameraDeviceHwl(
        uint32_t camera_id, std::unique_ptr<CameraDeviceHwl>* camera_device_hwl) {
    std::shared_ptr<libcamera::Camera> camera = nullptr;
    if (camera_device_hwl == nullptr) {
        ALOGE("%s: camera_device_hwl is nullptr.", __func__);
        return BAD_VALUE;
    }

    for (auto& t : cameraIdMap_) {
        ALOGI("%s: camera_id %d, check camera %p, %s, id %d", __func__, camera_id, t.first.get(),
              t.first->id().c_str(), t.second);
        if (t.second == camera_id) {
            camera = t.first;
            break;
        }
    }

    if (camera == nullptr) {
        ALOGE("%s: no camera for camera_id %d", __func__, camera_id);
        return BAD_VALUE;
    }

    ALOGI("%s: camera_id %u, camera_id_maps size %zu", __func__, camera_id,
          camera_id_maps[camera_id].size());
    CameraSensorMetadata cam_metadata = mCameraDef.camera_metadata_vec[camera_id];

    std::vector<std::shared_ptr<char*>> devPaths;
    std::vector<uint32_t> physicalIds;
    auto target_physical_devices = std::make_unique<PhysicalDeviceMap>();
    if (camera_id_maps[camera_id].size() >= 2) {
        // Here only map the physical cameras' CameraSensorMetadata under the logical camera!
        for (const auto& physical_device : camera_id_maps[camera_id]) {
            ALOGI("%s: physical_device.second %d", __func__, physical_device.second);
            target_physical_devices
                    ->emplace(physical_device.second,
                              std::make_pair(physical_device.first,
                                             std::unique_ptr<CameraSensorMetadata>(
                                                     new CameraSensorMetadata(
                                                             mCameraDef.camera_metadata_vec
                                                                     [physical_device.second]))));
        }

        for (const auto& physical_device : *target_physical_devices) {
            ALOGI("%s: devPaths.push_back %s, id %d", __func__,
                  mSets[physical_device.first].mDevPath, physical_device.first);
            devPaths.push_back(std::make_shared<char*>(mSets[physical_device.first].mDevPath));
            physicalIds.push_back(physical_device.first);
        }
    } else {
        devPaths.push_back(std::make_shared<char*>(mSets[camera_id].mDevPath));
        physicalIds.push_back(0);
    }

    *camera_device_hwl =
            CameraDeviceHwlImpl::Create(camera, camera_id, mCameraDef.cam_blit_copy_hw,
                                        mCameraDef.cam_blit_csc_hw, mCameraDef.jpeg_hw.c_str(),
                                        mCameraDef.mUseCpuEncoder, &cam_metadata,
                                        std::move(target_physical_devices), mCallback);

    if (*camera_device_hwl == nullptr) {
        ALOGE("%s: Cannot create CameraDeviceHWlImpl.", __func__);
        return BAD_VALUE;
    }

    device_map[camera_id] = (*camera_device_hwl).get();

    return OK;
}

status_t CameraProviderHwlImpl::CreateBufferAllocatorHwl(
        std::unique_ptr<CameraBufferAllocatorHwl>* camera_buffer_allocator_hwl) {
    if (camera_buffer_allocator_hwl == nullptr) {
        ALOGE("%s: camera_buffer_allocator_hwl is nullptr.", __func__);
        return BAD_VALUE;
    }

    return INVALID_OPERATION;
}

status_t CameraProviderHwlImpl::GetConcurrentStreamingCameraIds(
        std::vector<std::unordered_set<uint32_t>>* combinations) {
    if (combinations == nullptr) {
        return BAD_VALUE;
    }

    std::unordered_set<uint32_t> candidate_ids;
    for (auto& id : camera_id_maps) {
        candidate_ids.insert(id.first);
    }

    combinations->emplace_back(std::move(candidate_ids));
    return OK;
}

status_t CameraProviderHwlImpl::IsConcurrentStreamCombinationSupported(
        const std::vector<CameraIdAndStreamConfiguration>& configs, bool* is_supported) {
    if (is_supported == NULL)
        return BAD_VALUE;

    *is_supported = false;

    // Judge the steam config by related camera. Not Judge the "combine" of 2 cameras.
    // If there do have hardware limits for 2 cameras to work at the same time, need cosider it.
    for (auto& config : configs) {
        auto iter = device_map.find(config.camera_id);
        if (iter == device_map.end()) {
            ALOGE("%s: Unknown camera id: %u", __func__, config.camera_id);
            return BAD_VALUE;
        }

        CameraDeviceHwl* pCamera = iter->second;
        if (pCamera == NULL) {
            ALOGE("%s: Unexpected, pCamera is null for id %d", __func__, config.camera_id);
            return BAD_VALUE;
        }

        bool bSupport;
        bSupport = pCamera->IsStreamCombinationSupported(config.stream_configuration);
        if (bSupport == false) {
            ALOGE("%s: stream config not supported by camera %d", __func__, config.camera_id);
            return BAD_VALUE;
        }
    }

    *is_supported = true;

    return OK;
}

void CameraProviderHwlImpl::cameraAdded(std::shared_ptr<libcamera::Camera> camera) {
    auto iter = cameraIdMap_.find(camera);
    if (iter != cameraIdMap_.end()) {
        ALOGW("%s: camera %p, %s already found, id %d", __func__, camera.get(),
              camera->id().c_str(), iter->second);
        return;
    }

    cameraIdMap_[camera] = cameraId_++;

    ALOGI("%s: camera num %d", __func__, cameraIdMap_.size());

    return;
}

void CameraProviderHwlImpl::cameraRemoved(std::shared_ptr<libcamera::Camera> camera) {
    unsigned int id;
    bool isCameraNew = false;

    auto iter = cameraIdMap_.find(camera);
    if (iter == cameraIdMap_.end()) {
        ALOGW("%s: camera %p, %s not found", __func__, camera.get(), camera->id().c_str());
        return;
    }

    cameraIdMap_.erase(camera);

    ALOGI("%s: camera num %d", __func__, cameraIdMap_.size());

    return;
}

} // namespace android
