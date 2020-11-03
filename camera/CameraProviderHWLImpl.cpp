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

//#define LOG_NDEBUG 0
#define LOG_TAG "CameraProviderHwlImpl"

#include "CameraProviderHWLImpl.h"
#include "CameraMetadata.h"

#include <android-base/file.h>
#include <android-base/strings.h>
#include <cutils/properties.h>
#include <hardware/camera_common.h>
#include <log/log.h>
#include <linux/videodev2.h>

#include "CameraDeviceHWLImpl.h"
#include "CameraDeviceSessionHWLImpl.h"
#include "vendor_tag_defs.h"

namespace android {

CameraProviderHwlImpl::~CameraProviderHwlImpl()
{
}

std::unique_ptr<CameraProviderHwlImpl> CameraProviderHwlImpl::Create()
{
    auto provider = std::unique_ptr<CameraProviderHwlImpl>(
        new CameraProviderHwlImpl());

    if (provider == nullptr) {
        ALOGE("%s: Creating CameraProviderHwlImpl failed.", __func__);
        return nullptr;
    }

    status_t res = provider->Initialize();
    if (res != OK) {
        ALOGE("%s: Initializing CameraProviderHwlImpl failed: %s (%d).",
              __func__,
              strerror(-res),
              res);
        return nullptr;
    }

    ALOGI("%s: Created CameraProviderHwlImpl", __func__);

    return provider;
}

status_t CameraProviderHwlImpl::Initialize()
{
    mCameraCfgParser.Init();
    mCameraDef = mCameraCfgParser.mcamera();

    ALOGI("%s, copy_hw %d, csc_hw %d, jpeg_hw %s, mplane %d, %d",
        __func__, mCameraDef.cam_blit_copy_hw, mCameraDef.cam_blit_csc_hw, mCameraDef.jpeg_hw.c_str(),
        mCameraDef.camera_metadata[0].mplane, mCameraDef.camera_metadata[1].mplane);

    // enumerate all camera sensors.
    enumSensorSet();

    // check if camera exists.
    for (int32_t index = BACK_CAM_ID; index < NUM_CAM_ID; index++) {
        ALOGI("%s, camera %d exist %d", __func__, index, mSets[index].mExisting);
        if (!mSets[index].mExisting) {
            continue;
        }
        camera_id_list.push_back(index);
    }

    return OK;
}

void CameraProviderHwlImpl::enumSensorSet()
{
    ALOGI("%s", __func__);
    // get back camera property.
    strncpy(mSets[BACK_CAM_ID].mPropertyName, mCameraDef.camera_metadata[BACK_CAM_ID].camera_name, strlen(mCameraDef.camera_metadata[BACK_CAM_ID].camera_name));
    mSets[BACK_CAM_ID].mOrientation = mCameraDef.camera_metadata[BACK_CAM_ID].orientation;
    mSets[BACK_CAM_ID].mFacing = CAMERA_FACING_BACK;
    mSets[BACK_CAM_ID].mExisting = false;

    // get front camera property.
    strncpy(mSets[FRONT_CAM_ID].mPropertyName, mCameraDef.camera_metadata[FRONT_CAM_ID].camera_name, strlen(mCameraDef.camera_metadata[FRONT_CAM_ID].camera_name));
    mSets[FRONT_CAM_ID].mOrientation = mCameraDef.camera_metadata[FRONT_CAM_ID].orientation;;
    mSets[FRONT_CAM_ID].mFacing = CAMERA_FACING_FRONT;
    mSets[FRONT_CAM_ID].mExisting = false;

    // make sure of back&front camera parameters.
    matchDevNodes();
}

int32_t CameraProviderHwlImpl::matchPropertyName(nodeSet* nodes, int32_t index)
{
    char *propName = NULL;
    int32_t ret = 0;

    if (nodes == NULL) {
        return 0;
    }

    propName = mSets[index].mPropertyName;
    ALOGI("matchPropertyName: index:%d, %s", index, propName);

    ret = matchNodeName(propName, nodes, index);
    if (ret != 0) {
        ALOGE("do not find proper valid video node to match the setting");
        return -1;
    }

    return 0;
}

static bool IsVideoCaptureDevice(const char* devNode)
{
    if(devNode == NULL)
        return false;

    int fd = open(devNode, O_RDONLY);
    if(fd < 0)
        return false;

    struct v4l2_fmtdesc vid_fmtdesc;
    vid_fmtdesc.index = 0;
    vid_fmtdesc.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    int ret = ioctl(fd, VIDIOC_ENUM_FMT, &vid_fmtdesc);
    close(fd);

    return (ret == 0) ? true : false;
}

int32_t CameraProviderHwlImpl::matchNodeName(const char* nodeName, nodeSet* nodes, int32_t index)
{
    if (nodes == NULL) {
        return -1;
    }

    const char* sensorName = NULL;
    const char* devNode = NULL;
    int32_t ret = -1;

    ALOGI("%s", __func__);
    nodeSet* node = nodes;
    while (node != NULL) {
        devNode = node->devNode;
        sensorName = node->nodeName;
        if (node->isHeld || strlen(sensorName) == 0) {
            node = node->next;
            continue;
        }

        ALOGI("matchNodeName: sensor:%s, dev:%s, node:%s, index:%d",
              sensorName, devNode, nodeName, index);

        CameraSensorMetadata *camera_metadata;
        camera_metadata = &(mCameraDef.camera_metadata[index]);
        if(camera_metadata->device_node[0] && strcmp(devNode, camera_metadata->device_node)) {
            ALOGI("matchNodeName: device node %s is not the given node %s", devNode, camera_metadata->device_node);
            node = node->next;
            continue;
        }

        if ((strlen(nodeName) == 0) || !strstr(sensorName, nodeName)) {
            node = node->next;
            continue;
        }

        strncpy(mSets[index].mSensorName, sensorName, PROPERTY_VALUE_MAX-1);
        strncpy(mSets[index].mDevPath, devNode, CAMAERA_FILENAME_LENGTH);
        ALOGI("Camera ID %d: name %s, Facing %d, orientation %d, dev path %s",
                index, mSets[index].mSensorName, mSets[index].mFacing,
                mSets[index].mOrientation, mSets[index].mDevPath);
        mSets[index].mExisting = true;
        node->isHeld = true;
        ret = 0;
        break;
    }

    return ret;
}

int32_t CameraProviderHwlImpl::matchDevNodes()
{
    DIR *vidDir = NULL;
    struct dirent *dirEntry;
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

        sprintf(node->devNode, "/dev/%s", dirEntry->d_name);

        getNodeName(node->devNode, node->nodeName, nameLen);

        if (strlen(node->nodeName) != 0) {
            if (nodes == NULL) {
                nodes = node;
            }
            else {
                last->next = node;
            }
            last = node;
        }
    }

    closedir(vidDir);

    for (int32_t index=0; index<MAX_CAMERAS; index++) {
        matchPropertyName(nodes, index);
    }

    node = nodes;
    while (node != NULL) {
        last = node->next;
        free(node);
        node = last;
    }

    return 0;
}

int32_t CameraProviderHwlImpl::getNodeName(const char* devNode, char name[], size_t length)
{
    int32_t ret = -1;
    int32_t fd = -1;
    size_t strLen = 0;
    struct v4l2_capability vidCap;
    struct v4l2_dbg_chip_ident vidChip;

    ALOGI("getNodeName: dev path:%s", devNode);
    if ((fd = open(devNode, O_RDWR, O_NONBLOCK)) < 0) {
        ALOGW("%s open dev path:%s failed:%s", __func__, devNode,
                strerror(errno));
        return ret;
    }

    ret = ioctl(fd, VIDIOC_QUERYCAP, &vidCap);
    if (ret < 0) {
        ALOGW("%s QUERYCAP dev path:%s failed", __func__, devNode);
        close(fd);
        fd = -1;
        return ret;
    }

    ALOGI("video capabilities 0x%x\n", vidCap.capabilities);
    if (!(vidCap.capabilities &
          (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE))) {
        ALOGW("%s dev path:%s is not capture", __func__, devNode);
        close(fd);
        fd = -1;
        ret = -1;
        return ret;
    }

    if(strstr((const char*)vidCap.driver, "uvc")) {
        bool IsVideoCapDev = IsVideoCaptureDevice(devNode);
        if(IsVideoCapDev == false) {
            ALOGI("Although %s driver name has uvc, but it's a uvc meta device", devNode);
            close(fd);
            fd = -1;
            return -1;
        }
    }

    strncat(name, (const char*)vidCap.driver, length);
    strLen = strlen((const char*)vidCap.driver);
    length -= strLen;
    ALOGI("getNodeName: node name:%s", name);

    ret = ioctl(fd, VIDIOC_DBG_G_CHIP_IDENT, &vidChip);
    if (ret < 0) {
        ALOGI("%s CHIP_IDENT dev path:%s failed", __func__, devNode);
        strncat(name, ",", length);
        strLen = 1;
        length -= strLen;
        strncat(name, (const char*)vidCap.card, length);
        ALOGI("getNodeNames: node name:%s", name);
        close(fd);

        fd = -1;
        return ret;
    }

    strncat(name, ",", length);
    strLen = 1;
    length -= strLen;
    strncat(name, vidChip.match.name, length);

    ALOGI("getNodeNames: node name:%s", name);
    close(fd);

    return ret;
}


status_t CameraProviderHwlImpl::SetCallback(
    const HwlCameraProviderCallback& callback)
{
    torch_cb_ = callback.torch_mode_status_change;
    physical_camera_status_cb_ = callback.physical_camera_device_status_change;

    return OK;
}

status_t CameraProviderHwlImpl::TriggerDeferredCallbacks()
{
    return OK;
}

status_t CameraProviderHwlImpl::GetVendorTags(
    std::vector<VendorTagSection>* vendor_tag_sections)
{
    if (vendor_tag_sections == nullptr) {
        ALOGE("%s: vendor_tag_sections is nullptr.", __func__);
        return BAD_VALUE;
    }

    // Todo: add vendor tags
    return OK;
}

status_t CameraProviderHwlImpl::GetVisibleCameraIds(
    std::vector<std::uint32_t>* camera_ids)
{
    if (camera_ids == nullptr) {
        ALOGE("%s: camera_ids is nullptr.", __func__);
        return BAD_VALUE;
    }

    camera_ids->assign(camera_id_list.begin(), camera_id_list.end());

    return OK;
}

status_t CameraProviderHwlImpl::CreateCameraDeviceHwl(
    uint32_t camera_id, std::unique_ptr<CameraDeviceHwl>* camera_device_hwl)
{
    if (camera_device_hwl == nullptr) {
        ALOGE("%s: camera_device_hwl is nullptr.", __func__);
        return BAD_VALUE;
    }

    auto it = std::find(camera_id_list.begin(), camera_id_list.end(), camera_id);
    if(it == camera_id_list.end()) {
        ALOGE("%s: camera_id %d invalid", __func__, camera_id);
        return BAD_VALUE;
    }

    *camera_device_hwl = CameraDeviceHwlImpl::Create(camera_id, mSets[camera_id].mDevPath,
      mCameraDef.cam_blit_copy_hw, mCameraDef.cam_blit_csc_hw, mCameraDef.jpeg_hw.c_str(), &mCameraDef.camera_metadata[camera_id]);

    if (*camera_device_hwl == nullptr) {
        ALOGE("%s: Cannot create CameraDeviceHWlImpl.", __func__);
        return BAD_VALUE;
    }

    device_map[camera_id] = (*camera_device_hwl).get();

    return OK;
}

status_t CameraProviderHwlImpl::CreateBufferAllocatorHwl(
    std::unique_ptr<CameraBufferAllocatorHwl>* camera_buffer_allocator_hwl)
{
    if (camera_buffer_allocator_hwl == nullptr) {
        ALOGE("%s: camera_buffer_allocator_hwl is nullptr.", __func__);
        return BAD_VALUE;
    }

    return INVALID_OPERATION;
}

status_t CameraProviderHwlImpl::GetConcurrentStreamingCameraIds(
    std::vector<std::unordered_set<uint32_t>>* combinations)
{
    if (combinations == nullptr) {
        return BAD_VALUE;
    }

    std::unordered_set<uint32_t> candidate_ids;

    for (auto id : camera_id_list) {
        candidate_ids.insert(id);
    }

    combinations->emplace_back(std::move(candidate_ids));
    return OK;
}

status_t CameraProviderHwlImpl::IsConcurrentStreamCombinationSupported(
        const std::vector<CameraIdAndStreamConfiguration>& configs, bool* is_supported)
{
    if(is_supported == NULL)
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

        CameraDeviceHwl *pCamera = iter->second;
        if(pCamera == NULL) {
            ALOGE("%s: Unexpected, pCamera is null for id %d", __func__, config.camera_id);
            return BAD_VALUE;
        }

        bool bSupport;
        bSupport = pCamera->IsStreamCombinationSupported(config.stream_configuration);
        if(bSupport == false) {
            ALOGE("%s: stream config not supported by camera %d", __func__, config.camera_id);
            return BAD_VALUE;
        }
    }

    *is_supported = true;

    return OK;
}
}  // namespace android
