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
    mCameraCfgParser.Init();
    mCameraDef = mCameraCfgParser.mcamera();
    memset(&mCallback, 0, sizeof(mCallback));

    // enumerate all camera sensors.
    enumSensorSet();

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

    // create singleton "ImageProcess" object, so the dlopen, g2d_open/cl_g2d_open
    // can be done in HW init, save time for CTS.
    fsl::ImageProcess::getInstance();

    return OK;
}

// virtual ~CameraProviderHwlImpl();
CameraProviderHwlImpl::~CameraProviderHwlImpl() {
    fsl::ImageProcess* imageProcess = fsl::ImageProcess::getInstance();
    delete imageProcess;

    WaitForStatusCallbackFuture();
}

void CameraProviderHwlImpl::enumSensorSet() {
    ALOGI("%s", __func__);

    int cam_meta_size = mCameraDef.camera_metadata_vec.size();

    mSets.resize(cam_meta_size);

    // basic camera
    int logical_cam_id = 0;
    for (auto iter = mCameraDef.camera_id_map_.begin(); iter != mCameraDef.camera_id_map_.end();
         ++iter) {
        int physical_cam_size = iter->second.size();
        ALOGI("%s: logical camera id is: %d; it's physical camera size is %d \n", __func__,
              logical_cam_id, physical_cam_size);
        if (physical_cam_size < 2) {
            // This is a basic camera definition
            CameraSensorMetadata basic_cam_meta = mCameraDef.camera_metadata_vec[logical_cam_id];

            strncpy(mSets[logical_cam_id].mPropertyName, basic_cam_meta.camera_name,
                    strlen(basic_cam_meta.camera_name));
            mSets[logical_cam_id].mOrientation = basic_cam_meta.orientation;
            // -1 means this is an invalid basic camera config node,
            // The node only can act as physical camera for logical camera group.
            if (strcmp(basic_cam_meta.camera_type, "back") == 0) {
                mSets[logical_cam_id].mFacing = CAMERA_FACING_BACK;
            } else if (strcmp(basic_cam_meta.camera_type, "front") == 0) {
                mSets[logical_cam_id].mFacing = CAMERA_FACING_FRONT;
            } else {
                mSets[logical_cam_id].mFacing = -1;
            }
            mSets[logical_cam_id].mExisting = false;
        }
        logical_cam_id++;
    }

    ALOGI("%s: mCameraDef.camera_metadata_vec size %lu", __func__,
          mCameraDef.camera_metadata_vec.size());

    // physical camera
    int first_physical_cam_id = MAX_BASIC_CAMERA_NUM; // 24
    // At least need two physical cameras to compose logical camera group
    if (cam_meta_size >= (MAX_BASIC_CAMERA_NUM + 1)) {
        for (auto physical_id = first_physical_cam_id; physical_id != cam_meta_size;
             ++physical_id) {
            CameraSensorMetadata physical_cam_meta = mCameraDef.camera_metadata_vec[physical_id];
            strncpy(mSets[physical_id].mPropertyName, physical_cam_meta.camera_name,
                    strlen(physical_cam_meta.camera_name));
            mSets[physical_id].mOrientation = physical_cam_meta.orientation;
            if (strcmp(physical_cam_meta.camera_type, "back") == 0)
                mSets[physical_id].mFacing = CAMERA_FACING_BACK;
            else
                mSets[physical_id].mFacing = CAMERA_FACING_FRONT;
            mSets[physical_id].mExisting = false;
        }
    }

    // make sure of back&front camera parameters.
    matchDevNodes();
}

int32_t CameraProviderHwlImpl::matchPropertyName(nodeSet* nodes, int32_t index) {
    char* propName = NULL;

    if (nodes == NULL) {
        return 0;
    }

    propName = mSets[index].mPropertyName;
    if (strlen(propName) == 0) {
        ALOGE("%s: No such camera%d defined in camera config json.", __func__, index);
        return -1;
    }

    const char* devNodeName = NULL;
    const char* devNode = NULL;
    const char* devBusInfo = NULL;
    nodeSet* node = nodes;

    while (node != NULL) {
        devNode = node->devNode;
        devNodeName = node->nodeName;
        devBusInfo = node->busInfo;

        // We may have one sensor binded to more than one camera instance
        // since it may act as 1 normal camera and also as physical camera for one logical camera
        if (/*node->isHeld || */ strlen(devNodeName) == 0) {
            node = node->next;
            continue;
        }

        ALOGI("%s: index:%d, target devName:%s, devNodeName:%s, devBusInfo:%s, devNode:%s",
              __func__, index, propName, devNodeName, devBusInfo, devNode);

        CameraSensorMetadata camera_metadata = mCameraDef.camera_metadata_vec[index];
        if (camera_metadata.bus_info[0] && strcmp(devBusInfo, camera_metadata.bus_info)) {
            ALOGI("%s: bus info unmatch, expect %s, actual %s", __func__, camera_metadata.bus_info,
                  devBusInfo);
            node = node->next;
            continue;
        }

        if (!strstr(devNodeName, propName)) {
            node = node->next;
            continue;
        }

        strncpy(mSets[index].mSensorName, devNodeName, PROPERTY_VALUE_MAX - 1);
        strncpy(mSets[index].mDevPath, devNode, CAMAERA_FILENAME_LENGTH);
        ALOGI("Camera index %d: name %s, Facing %d, orientation %d, dev path %s", index,
              mSets[index].mSensorName, mSets[index].mFacing, mSets[index].mOrientation,
              mSets[index].mDevPath);
        mSets[index].mExisting = true;
        node->isHeld = true;
        break;
    }

    return 0;
}

int32_t CameraProviderHwlImpl::matchDevNodes() {
    DIR* vidDir = NULL;
    struct dirent* dirEntry;
    char mCamDevice[64];
    std::string buffer;
    size_t nameLen = CAMERA_SENSOR_LENGTH - 1;
    nodeSet *nodes = NULL, *node = NULL, *last = NULL;

    ALOGI("%s", __func__);
    vidDir = opendir("/sys/class/video4linux");
    if (vidDir == NULL) {
        return -1;
    }

    while ((dirEntry = readdir(vidDir)) != NULL) {
        if (strncmp(dirEntry->d_name, "video", 5)) {
            continue;
        }

        node = (nodeSet*)malloc(sizeof(nodeSet));
        if (node == NULL) {
            ALOGE("%s malloc failed", __func__);
            break;
        }

        memset(node, 0, sizeof(nodeSet));

        sprintf(mCamDevice, "/sys/class/video4linux/%s/name", dirEntry->d_name);
        if (!android::base::ReadFileToString(std::string(mCamDevice), &buffer)) {
            free(node);
            ALOGW("can't read video device name");
            continue;
        }
        ALOGI("%s: /sys/class/video4linux/%s/name is %s", __func__, dirEntry->d_name,
              buffer.c_str());

        // Use "/sys/class/video4linux/%s/name" to filter out unsupported video devices.
        // So no need to open none-camera devices such as VPU.
        // If it's neithor basic camera nor physical camera, skip.
        bool bOnBoardCamera = false;

        // basic camera
        for (int32_t index = 0; index < (int32_t)mCameraDef.camera_id_map_.size(); index++) {
            if (strstr(buffer.c_str(), mSets[index].mPropertyName)) {
                bOnBoardCamera = true;
                break;
            }
        }

        // physical camera
        for (int32_t phyindex = MAX_BASIC_CAMERA_NUM;
             phyindex < (int32_t)mCameraDef.camera_metadata_vec.size(); phyindex++) {
            if (strstr(buffer.c_str(), mSets[phyindex].mPropertyName)) {
                bOnBoardCamera = true;
                break;
            }
        }

        if (bOnBoardCamera == false) {
            ALOGI("%s: %s is not on-board camera deivce, skip", __func__, dirEntry->d_name);
            free(node);
            continue;
        }

        // open camera node to query
        sprintf(node->devNode, "/dev/%s", dirEntry->d_name);
        int32_t ret = getNodeName(node->devNode, node->nodeName, nameLen, node->busInfo, nameLen);
        if (ret < 0) {
            ALOGW("%s: dev path %s getNodeName failed", __func__, node->devNode);
            free(node);
            continue;
        }

        // string read from ReadFileToString have '\n' in last byte
        // so we just need copy (buffer.length() - 1) length
        strncat(node->nodeName, buffer.c_str(), (buffer.length() - 1));
        ALOGI("NodeName: node name:%s \n", node->nodeName);

        if (strlen(node->nodeName) != 0) {
            if (nodes == NULL) {
                nodes = node;
            } else {
                last->next = node;
            }
            last = node;
        }
    }

    closedir(vidDir);

    // basic camera
    for (int32_t index = 0; index < (int32_t)mCameraDef.camera_id_map_.size(); index++) {
        matchPropertyName(nodes, index);
    }

    // physical camera
    for (int32_t phyindex = MAX_BASIC_CAMERA_NUM;
         phyindex < (int32_t)mCameraDef.camera_metadata_vec.size(); phyindex++) {
        matchPropertyName(nodes, phyindex);
    }

    node = nodes;
    while (node != NULL) {
        last = node->next;
        free(node);
        node = last;
    }

    return 0;
}

int32_t CameraProviderHwlImpl::getNodeName(const char* devNode, char name[], size_t length,
                                           char busInfo[], size_t busInfoLen) {
    int32_t ret = -1;
    size_t strLen = 0;
    struct v4l2_capability vidCap;

    ALOGI("getNodeName: dev path:%s", devNode);

    base::unique_fd fd(::open(devNode, O_RDWR | O_NONBLOCK));
    if (fd.get() < 0) {
        ALOGE("%s open dev path:%s failed:%s", __func__, devNode, strerror(errno));
        return -1;
    }

    ret = ioctl(fd.get(), VIDIOC_QUERYCAP, &vidCap);
    if (ret < 0) {
        ALOGW("%s QUERYCAP dev path:%s failed", __func__, devNode);
        return ret;
    }

    ALOGI("video capabilities 0x%x\n", vidCap.capabilities);
    if (!(vidCap.capabilities & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE))) {
        ALOGW("%s dev path:%s is not capture", __func__, devNode);
        return -1;
    }

    strncpy(busInfo, (const char*)vidCap.bus_info, busInfoLen);

    strncat(name, (const char*)vidCap.driver, length);
    strLen = strlen((const char*)vidCap.driver);
    length -= strLen;
    ALOGI("getNodeName: node name:%s, bus info: %s", name, vidCap.bus_info);

    strncat(name, ",", length);

    return ret;
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

    for (const auto& device : camera_id_maps) {
        camera_ids->push_back(device.first);
    }

    return OK;
}

status_t CameraProviderHwlImpl::CreateCameraDeviceHwl(
        uint32_t camera_id, std::unique_ptr<CameraDeviceHwl>* camera_device_hwl) {
    if (camera_device_hwl == nullptr) {
        ALOGE("%s: camera_device_hwl is nullptr.", __func__);
        return BAD_VALUE;
    }

    if (camera_id_maps.find(camera_id) == camera_id_maps.end()) {
        ALOGE("%s: Invalid camera id: %u", __func__, camera_id);
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
            CameraDeviceHwlImpl::Create(camera_id, std::move(devPaths), std::move(physicalIds),
                                        mCameraDef.cam_blit_copy_hw, mCameraDef.cam_blit_csc_hw,
                                        mCameraDef.jpeg_hw.c_str(), mCameraDef.mUseCpuEncoder,
                                        &cam_metadata, std::move(target_physical_devices),
                                        mCallback);

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

} // namespace android
