/*
 * Copyright 2019-2020 NXP
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
#include "CameraConfigurationParser.h"

#define LOG_TAG "CameraConfigurationParser"

#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <cutils/properties.h>
#include <json/json.h>
#include <json/reader.h>
#include <log/log.h>
#include <stdlib.h>

#include <string>

constexpr char kSocType[] = "ro.boot.soc_type";

namespace cameraconfigparser {
namespace {
////////////////////// Device Personality keys //////////////////////
//
// **** Camera ****
//
// Example segment (transcribed to constants):
//
//
//  "__readme": [
//    "Basic Camera HAL v3 configuration."
//  ],

//  "hal_version": "3",
//  "cam_blit_copy": "DPU",
//  "cam_blit_csc": "GPU_3D",

//  "camera_metadata": [
//    {
//      "camera_type": "back"
//      "camera_name": "imx8_ov5640",
//      "buffer_type": "dma",
//      "ActiveArrayWidth": "2592",
//      "ActiveArrayHeight": "1944",
//      "PixelArrayWidth": "2592",
//      "PixelArrayHeight": "1944",
//      "PhysicalWidth": "3.6288",
//      "PhysicalHeight": "2.7216",
//      "FocalLength": "3.37",
//      "MaxJpegSize": "8388608",
//      "MinFrameDuration": "33331760",
//      "MaxFrameDuration": "300000000"
//    },
//    {
//    "camera_type": "front",
//    "camera_name": "imx8_ov5640",
//    "buffer_type": "dma",
//    "ActiveArrayWidth": "2592",
//    "ActiveArrayHeight": "1944",
//    "PixelArrayWidth": "2592",
//    "PixelArrayHeight": "1944",
//    "PhysicalWidth": "3.6288",
//    "PhysicalHeight": "2.7216",
//    "FocalLength": "3.37",
//    "MaxJpegSize": "8388608",
//    "MinFrameDuration": "33331760",
//    "MaxFrameDuration": "300000000"
//  }
// Location of the camera json configuration files.
const char* const kCameraConfiguration = "/vendor/etc/configs/camera_config";

//
// Array of camera definitions for all cameras available on the device (array).
// Top Level Key.
const char* const kCameraBufferType = "buffer_type";

// Camera HAL version of currently defined camera (int).
// Currently supported values:
// - 1 (Camera HALv1)
// - 2 (Camera HALv2)
// - 3 (Camera HALv3)
const char* const kCameraDefinitionHalVersionKey = "hal_version";
const char* const kCameraBlitCopyKey = "cam_blit_copy";
const char* const kCameraBlitCscKey = "cam_blit_csc";
const char* const kCameraHwJpeg = "hw_jpeg_enc";
const char* const kUseCpuEncoder = "use_cpu_encoder";

const char* const kCameraMetadataKey = "camera_metadata";
const char* const kCameraTypeKey = "camera_type";
const char* const kMPlaneKey = "mplane";
const char* const kCameraNameKey = "camera_name";
const char* const kDeviceNodeKey = "device_node";
const char* const kBusInfoKey = "bus_info";
const char* const kOrientationKey = "orientation";
const char* const kV4l2FormatKey = "v4l2_format";

const char* const kActiveArrayWidthKey = "ActiveArrayWidth";
const char* const kActiveArrayHeightKey = "ActiveArrayHeight";
const char* const kPixelArrayWidthKey = "PixelArrayWidth";
const char* const kPixelArrayHeightKey = "PixelArrayHeight";
const char* const kPhysicalWidthKey = "PhysicalWidth";
const char* const kPhysicalHeightKey = "PhysicalHeight";
const char* const kFocalLengthKey = "FocalLength";
const char* const kMaxJpegSizeKey = "MaxJpegSize";
const char* const kMinFrameDurationKey = "MinFrameDuration";
const char* const kMaxFrameDurationKey = "MaxFrameDuration";
const char* const kMinFrameDurationKey_blob_5M = "MinFrameDurationBlob5M";
const char* const kOmitFrameKey = "OmitFrame";
const char* const kOmitFrameWidthKey = "width";
const char* const kOmitFrameHeightKey = "height";
const char* const kOmitFrameNumKey = "omit_num";

const char* const kAeCompMinKey = "ae_comp_min";
const char* const kAeCompMaxKey = "ae_comp_max";
const char* const kAeCompStepNumerator = "ae_comp_step_nummerator";
const char* const kAeCompStepDenominator = "ae_comp_step_denominator";
const char* const kExposureNsMin = "exposure_ns_min";
const char* const kExposureNsMax = "exposure_ns_max";
const char* const kAvailableCapabilitiesKey = "AvailableCapabilities";
const char* const kPhysicalNames = "PhysicalNames";
const char* const kMaxWidth = "MaxWidth";
const char* const kMaxHeight = "MaxHeight";
const char* const kMinWidth = "MinWidth";
const char* const kMinHeight = "MinHeight";
const char* const kGivenResKey = "GivenRes";
const char* const kGivenResWidthKey = "width";
const char* const kGivenResHeightKey = "height";

#define CSC_HW_GPU_2D "GPU_2D"
#define CSC_HW_GPU_3D "GPU_3D"
#define CSC_HW_IPU "IPU"
#define CSC_HW_PXP "PXP"
#define CSC_HW_DPU "DPU"
#define CSC_HW_CPU "CPU"

#define MAX_SENSOR_WIDTH INT_MAX
#define MAX_SENSOR_HEIGHT INT_MAX

HalVersion ValueToCameraHalVersion(const std::string& value) {
    int temp;
    char* endptr;
    HalVersion hal_version = kHalV1;

    temp = strtol(value.c_str(), &endptr, 10);
    if (*endptr != '\0') {
        ALOGE("%s: Invalid camera HAL version. Expected number, got %s.", __func__, value.c_str());
        return hal_version;
    }

    switch (temp) {
        case 1:
            hal_version = kHalV1;
            break;

        case 2:
            hal_version = kHalV2;
            break;

        case 3:
            hal_version = kHalV3;
            break;

        default:
            ALOGE("%s: Invalid camera HAL version. Version %d not supported.", __func__, temp);
    }

    return hal_version;
}

CscHw ValueToCameraCscHw(const std::string& value) {
    CscHw csc_hw = GPU_2D;
    if (value == CSC_HW_DPU) {
        csc_hw = DPU;
    } else if (value == CSC_HW_GPU_2D) {
        csc_hw = GPU_2D;
    } else if (value == CSC_HW_GPU_3D) {
        csc_hw = GPU_3D;
    } else if (value == CSC_HW_PXP) {
        csc_hw = PXP;
    } else if (value == CSC_HW_IPU) {
        csc_hw = IPU;
    } else if (value == CSC_HW_CPU) {
        csc_hw = CPU;
    }
    return csc_hw;
}

// Convert string value to buffer map type
bool ValueToCameraBufferType(const std::string& value, CameraSensorMetadata& static_meta) {
    if (value == "mmap") {
        static_meta.buffer_type = CameraSensorMetadata::kMmap;
        return true;
    } else if (value == "dma") {
        static_meta.buffer_type = CameraSensorMetadata::kDma;
        return true;
    }
    return false;
}

bool ParseCharacteristics(CameraDefinition* camera, const Json::Value& root, size_t meta_size,
                          ssize_t id);

// Parse camera definitions.
// Returns true, if definitions were sane.
bool ConfigureCameras(const Json::Value& value, CameraDefinition* camera) {
    if (!value.isObject()) {
        ALOGE("%s: Configuration root is not an object", __func__);
        return false;
    }

    if (!value.isMember(kCameraDefinitionHalVersionKey)) return true;
    camera->hal_version = ValueToCameraHalVersion(value[kCameraDefinitionHalVersionKey].asString());

    if (camera->hal_version != kHalV3) {
        ALOGE("%s: Invalid camera hal version: key %s is not matched.", __func__,
              kCameraDefinitionHalVersionKey);
        return false;
    }

    if (!value.isMember(kCameraBlitCopyKey)) return true;
    camera->cam_blit_copy_hw = ValueToCameraCscHw(value[kCameraBlitCopyKey].asString());

    if (!value.isMember(kCameraBlitCscKey)) return true;
    camera->cam_blit_csc_hw = ValueToCameraCscHw(value[kCameraBlitCscKey].asString());

    if (value.isMember(kCameraHwJpeg)) {
        camera->jpeg_hw = value[kCameraHwJpeg].asString();
    }

    if (value.isMember(kUseCpuEncoder))
        camera->mUseCpuEncoder = strtol(value[kUseCpuEncoder].asString().c_str(), NULL, 10);
    else
        camera->mUseCpuEncoder = 0;

    ALOGI("%s: mUseCpuEncoder %d", __func__, camera->mUseCpuEncoder);

    int camera_id = 0;
    size_t meta_size = value[kCameraMetadataKey].size();
    for (Json::ValueConstIterator device_iter = value[kCameraMetadataKey].begin();
         device_iter != value[kCameraMetadataKey].end(); ++device_iter) {
        // Parse json for each camera metadata definition.
        // Each camera definition can be taken as a logical camera or basic camera
        // which depending on if the camera set kPhysicalNames metadata correctly
        ParseCharacteristics(camera, *device_iter, meta_size, camera_id);

        camera->camera_id_map_.emplace(camera_id,
                                       std::vector<std::pair<CameraDeviceStatus, uint32_t>>());
        camera_id++;
    }

    int camera_index = 0;
    for (Json::ValueConstIterator device_iter = value[kCameraMetadataKey].begin();
         device_iter != value[kCameraMetadataKey].end(); ++device_iter) {
        if ((device_iter->isMember(kPhysicalNames)) &&
            ((*device_iter)[kPhysicalNames].size()) >= 2) {
            // This is an logical camera definition, update it's physical camera
            camera->camera_id_map_[camera_index].reserve((*device_iter)[kPhysicalNames].size());

            // For each defined physical camera name, check for match camera id and map the physical
            // camera id
            for (Json::ValueConstIterator phyiter = (*device_iter)[kPhysicalNames].begin();
                 phyiter != (*device_iter)[kPhysicalNames].end(); ++phyiter) {
                std::vector<CameraSensorMetadata>::iterator veciter;
                for (veciter = camera->camera_metadata_vec.begin();
                     veciter != camera->camera_metadata_vec.begin() + camera->camera_id_map_.size();
                     veciter++) {
                    CameraSensorMetadata temp = *veciter;
                    if (!strcmp(temp.camera_name, (char*)(*phyiter).asString().c_str())) {
                        strncpy(temp.camera_type,
                                camera->camera_metadata_vec[camera_index].camera_type,
                                META_STRING_SIZE);
                        temp.camera_type[META_STRING_SIZE - 1] = 0;
                        camera->camera_metadata_vec.push_back(temp);
                        int phy_camera_id = camera->camera_metadata_vec.size() - 1;
                        auto device_status = CameraDeviceStatus::kPresent;
                        camera->camera_id_map_[camera_index].push_back(
                                std::make_pair(device_status, phy_camera_id));
                        ALOGI("%s: camera_id_map_, camera_index %d, phy_camera_id %d, name %s, "
                              "camera->camera_metadata_vec size %zu",
                              __func__, camera_index, phy_camera_id, temp.camera_name,
                              camera->camera_metadata_vec.size());
                        break;
                    }
                }
            }
        }
        camera_index++;
    }

    return true;
}

bool ParseCharacteristics(CameraDefinition* camera, const Json::Value& root,
                          size_t meta_size __unused, ssize_t id) {
    CameraSensorMetadata static_meta[2];
    int cam_index = 0;
    uint32_t camera_id = id;
    char* endptr;
    bool is_logical = true;

    memset(static_meta, 0, sizeof(static_meta));

    // If one camera definition has no kCameraTypeKey meta, then it can only act as a physical
    // camera for logical cameras
    if (root.isMember(kCameraTypeKey)) {
        strncpy(static_meta[cam_index].camera_type, root[kCameraTypeKey].asString().c_str(),
                META_STRING_SIZE);
        static_meta[cam_index].camera_type[META_STRING_SIZE - 1] = 0;
    } else {
        is_logical = false;
        strncpy(static_meta[cam_index].camera_type, "physical", META_STRING_SIZE);
        static_meta[cam_index].camera_type[META_STRING_SIZE - 1] = 0;
    }

    // If one camera definition has no kCameraNameKey meta, then it's a logical camera
    if (root.isMember(kCameraNameKey)) {
        strncpy(static_meta[cam_index].camera_name, root[kCameraNameKey].asString().c_str(),
                META_STRING_SIZE);
        static_meta[cam_index].camera_name[META_STRING_SIZE - 1] = 0;
    } else {
        strncpy(static_meta[cam_index].camera_name, "logical", META_STRING_SIZE);
        static_meta[cam_index].camera_name[META_STRING_SIZE - 1] = 0;
    }

    if (root.isMember(kDeviceNodeKey)) {
        strncpy(static_meta[cam_index].device_node, root[kDeviceNodeKey].asString().c_str(),
                META_STRING_SIZE);
        static_meta[cam_index].device_node[META_STRING_SIZE - 1] = 0;
    } else
        static_meta[cam_index].device_node[0] = 0;

    if (root.isMember(kBusInfoKey)) {
        strncpy(static_meta[cam_index].bus_info, root[kBusInfoKey].asString().c_str(),
                META_STRING_SIZE);
        static_meta[cam_index].bus_info[META_STRING_SIZE - 1] = 0;
    } else
        static_meta[cam_index].bus_info[0] = 0;

    if (root.isMember(kV4l2FormatKey)) {
        strncpy(static_meta[cam_index].v4l2_format, root[kV4l2FormatKey].asString().c_str(),
                META_STRING_SIZE);
        static_meta[cam_index].v4l2_format[META_STRING_SIZE - 1] = 0;
    } else
        strncpy(static_meta[cam_index].v4l2_format, "yuyv", META_STRING_SIZE);

    if (!ValueToCameraBufferType(root[kCameraBufferType].asString(), static_meta[cam_index]))
        return false;

    if (root.isMember(kMPlaneKey)) {
        static_meta[cam_index].mplane = strtol(root[kMPlaneKey].asString().c_str(), NULL, 10);
    } else
        static_meta[cam_index].mplane = 0;

    std::string kOrientationKeyStr = root[kOrientationKey].asString();
    const char* kOrientationData = kOrientationKeyStr.c_str();
    static_meta[cam_index].orientation = strtol(kOrientationData, &endptr, 10);
    if (*endptr != '\0') {
        ALOGE("%s: Invalid camera orientation. got %s.", __func__, kOrientationData);
    }

    std::string kActiveArrayWidthStr = root[kActiveArrayWidthKey].asString();
    const char* kActiveArrayWidthData = kActiveArrayWidthStr.c_str();
    static_meta[cam_index].activearraywidth = strtol(kActiveArrayWidthData, &endptr, 10);
    if (*endptr != '\0') {
        ALOGE("%s: Invalid camera activearraywidth. got %s.", __func__, kActiveArrayWidthData);
    }

    std::string kActiveArrayHeightStr = root[kActiveArrayHeightKey].asString();
    const char* kActiveArrayHeightData = kActiveArrayHeightStr.c_str();
    static_meta[cam_index].activearrayheight = strtol(kActiveArrayHeightData, &endptr, 10);
    if (*endptr != '\0') {
        ALOGE("%s: Invalid camera ActiveArrayHeight. got %s.", __func__, kActiveArrayHeightData);
    }

    std::string kPixelArrayWidthStr = root[kPixelArrayWidthKey].asString();
    const char* kPixelArrayWidthData = kPixelArrayWidthStr.c_str();
    static_meta[cam_index].pixelarraywidth = strtol(kPixelArrayWidthData, &endptr, 10);
    if (*endptr != '\0') {
        ALOGE("%s: Invalid camera PixelArrayWidth. got %s.", __func__, kPixelArrayWidthData);
    }

    std::string kPixelArrayHeightStr = root[kPixelArrayHeightKey].asString();
    const char* kPixelArrayHeightData = kPixelArrayHeightStr.c_str();
    static_meta[cam_index].pixelarrayheight = strtol(kPixelArrayHeightData, &endptr, 10);
    if (*endptr != '\0') {
        ALOGE("%s: Invalid camera PixelArrayHeight. got %s.", __func__, kPixelArrayHeightData);
    }

    std::string kMaxJpegSizeStr = root[kMaxJpegSizeKey].asString();
    const char* kMaxJpegSizeData = kMaxJpegSizeStr.c_str();
    static_meta[cam_index].maxjpegsize = strtol(kMaxJpegSizeData, &endptr, 10);
    if (*endptr != '\0') {
        ALOGE("%s: Invalid camera MaxJpegSize. got %s.", __func__, kMaxJpegSizeData);
    }

    std::string kMinFrameDurationStr = root[kMinFrameDurationKey].asString();
    const char* kMinFrameDurationData = kMinFrameDurationStr.c_str();
    static_meta[cam_index].minframeduration = strtol(kMinFrameDurationData, &endptr, 10);
    if (*endptr != '\0') {
        ALOGE("%s: Invalid camera MinFrameDuration. got %s.", __func__, kMinFrameDurationData);
    }

    std::string kMaxFrameDurationStr = root[kMaxFrameDurationKey].asString();
    const char* kMaxFrameDurationData = kMaxFrameDurationStr.c_str();
    static_meta[cam_index].maxframeduration = strtol(kMaxFrameDurationData, &endptr, 10);
    if (*endptr != '\0') {
        ALOGE("%s: Invalid camera MaxFrameDuration. got %s.", __func__, kMaxFrameDurationData);
    }

    if (root.isMember(kMinFrameDurationKey_blob_5M)) {
        std::string kMinFrameDurationStr_blob_5M = root[kMinFrameDurationKey_blob_5M].asString();
        const char* kMinFrameDurationData_blob_5M = kMinFrameDurationStr_blob_5M.c_str();
        static_meta[cam_index].minframeduration_blob_5M =
                strtol(kMinFrameDurationData_blob_5M, &endptr, 10);
        if (*endptr != '\0') {
            ALOGE("%s: Invalid camera MinFrameDuration. got %s.", __func__,
                  kMinFrameDurationData_blob_5M);
            static_meta[cam_index].minframeduration_blob_5M = 0;
        }
    } else
        static_meta[cam_index].minframeduration_blob_5M = 0;

    std::string kPhysicalWidthStr = root[kPhysicalWidthKey].asString();
    const char* kPhysicalWidthData = kPhysicalWidthStr.c_str();
    static_meta[cam_index].physicalwidth = strtof(kPhysicalWidthData, &endptr);
    if (*endptr != '\0') {
        ALOGE("%s: Invalid camera PhysicalWidth. got %s.", __func__, kPhysicalWidthData);
    }

    std::string kPhysicalHeightStr = root[kPhysicalHeightKey].asString();
    const char* kPhysicalHeightData = kPhysicalHeightStr.c_str();
    static_meta[cam_index].physicalheight = strtof(kPhysicalHeightData, &endptr);
    if (*endptr != '\0') {
        ALOGE("%s: Invalid PhysicalHeight. got %s.", __func__, kPhysicalHeightData);
    }

    std::string kFocalLengthStr = root[kFocalLengthKey].asString();
    const char* kFocalLengthData = kFocalLengthStr.c_str();
    static_meta[cam_index].focallength = strtof(kFocalLengthData, &endptr);
    if (*endptr != '\0') {
        ALOGE("%s: Invalid camera FocalLength. got %s.", __func__, kFocalLengthData);
    }

    if (root.isMember(kAeCompMinKey))
        static_meta[cam_index].mAeCompMin =
                strtol(root[kAeCompMinKey].asString().c_str(), NULL, 10);
    else
        static_meta[cam_index].mAeCompMin = -3;

    if (root.isMember(kAeCompMaxKey))
        static_meta[cam_index].mAeCompMax =
                strtol(root[kAeCompMaxKey].asString().c_str(), NULL, 10);
    else
        static_meta[cam_index].mAeCompMax = 3;

    if (root.isMember(kAeCompStepNumerator))
        static_meta[cam_index].mAeCompStepNumerator =
                strtol(root[kAeCompStepNumerator].asString().c_str(), NULL, 10);
    else
        static_meta[cam_index].mAeCompStepNumerator = 1;

    if (root.isMember(kAeCompStepDenominator))
        static_meta[cam_index].mAeCompStepDenominator =
                strtol(root[kAeCompStepDenominator].asString().c_str(), NULL, 10);
    else
        static_meta[cam_index].mAeCompStepDenominator = 1;

    if (root.isMember(kAvailableCapabilitiesKey)) {
        static_meta[cam_index].mAvailableCapabilities =
                strtol(root[kAvailableCapabilitiesKey].asString().c_str(), NULL, 10);
    } else {
        static_meta[cam_index].mAvailableCapabilities = 0;
    }

    if (root.isMember(kExposureNsMin))
        static_meta[cam_index].mExposureNsMin =
                (int64_t)strtoll(root[kExposureNsMin].asString().c_str(), NULL, 10);
    else
        static_meta[cam_index].mExposureNsMin = 1000L;

    if (root.isMember(kExposureNsMax))
        static_meta[cam_index].mExposureNsMax =
                (int64_t)strtoll(root[kExposureNsMax].asString().c_str(), NULL, 10);
    else
        static_meta[cam_index].mExposureNsMax = 300000000L;

    if (root.isMember(kMaxWidth))
        static_meta[cam_index].mMaxWidth = strtol(root[kMaxWidth].asString().c_str(), NULL, 10);
    else
        static_meta[cam_index].mMaxWidth = MAX_SENSOR_WIDTH;

    if (root.isMember(kMaxHeight))
        static_meta[cam_index].mMaxHeight = strtol(root[kMaxHeight].asString().c_str(), NULL, 10);
    else
        static_meta[cam_index].mMaxHeight = MAX_SENSOR_HEIGHT;

    if (root.isMember(kMinWidth))
        static_meta[cam_index].mMinWidth = strtol(root[kMinWidth].asString().c_str(), NULL, 10);
    else
        static_meta[cam_index].mMinWidth = 0;

    if (root.isMember(kMinHeight))
        static_meta[cam_index].mMinHeight = strtol(root[kMinHeight].asString().c_str(), NULL, 10);
    else
        static_meta[cam_index].mMinHeight = 0;

    ALOGI("%s: res min %dx%d, max %dx%d", __func__, static_meta[cam_index].mMinWidth,
          static_meta[cam_index].mMinHeight, static_meta[cam_index].mMaxWidth,
          static_meta[cam_index].mMaxHeight);

    int given_res_index = 0;
    for (Json::ValueConstIterator resIter = root[kGivenResKey].begin();
         resIter != root[kGivenResKey].end(); ++resIter) {
        std::string kGivenResWidthStr = (*resIter)[kGivenResWidthKey].asString();
        const char* kGivenResWidthData = kGivenResWidthStr.c_str();
        static_meta[cam_index].mGivenRes[given_res_index].width =
                strtol(kGivenResWidthData, &endptr, 10);
        if (*endptr != '\0') {
            ALOGE("%s: Invalid camera given width. got %s.", __func__, kGivenResWidthData);
        }

        std::string kGivenResHeightStr = (*resIter)[kGivenResHeightKey].asString();
        const char* kGivenResHeightData = kGivenResHeightStr.c_str();
        static_meta[cam_index].mGivenRes[given_res_index].height =
                strtol(kGivenResHeightData, &endptr, 10);
        if (*endptr != '\0') {
            ALOGE("%s: Invalid camera given height. got %s.", __func__, kGivenResHeightData);
        }

        ALOGI("%s: given resolution %dx%d", __func__,
              static_meta[cam_index].mGivenRes[given_res_index].width,
              static_meta[cam_index].mGivenRes[given_res_index].height);

        given_res_index++;
        if (given_res_index >= GIVEN_RESOLUTION_NUM) break;
    }
    static_meta[cam_index].mGivenResNum = given_res_index;

    int omit_index = 0;
    for (Json::ValueConstIterator omititer = root[kOmitFrameKey].begin();
         omititer != root[kOmitFrameKey].end(); ++omititer) {
        std::string kOmitFrameWidthStr = (*omititer)[kOmitFrameWidthKey].asString();
        const char* kOmitFrameWidthData = kOmitFrameWidthStr.c_str();
        static_meta[cam_index].omit_frame[omit_index].width =
                strtol(kOmitFrameWidthData, &endptr, 10);
        if (*endptr != '\0') {
            ALOGE("%s: Invalid camera omitframewidth. got %s.", __func__, kOmitFrameWidthData);
        }

        std::string kOmitFrameHeightStr = (*omititer)[kOmitFrameHeightKey].asString();
        const char* kOmitFrameHeightData = kOmitFrameHeightStr.c_str();
        static_meta[cam_index].omit_frame[omit_index].height =
                strtol(kOmitFrameHeightData, &endptr, 10);
        if (*endptr != '\0') {
            ALOGE("%s: Invalid camera omitframeheight. got %s.", __func__, kOmitFrameHeightData);
        }

        std::string kOmitFrameNumStr = (*omititer)[kOmitFrameNumKey].asString();
        const char* kOmitFrameNumData = kOmitFrameNumStr.c_str();
        static_meta[cam_index].omit_frame[omit_index].omitnum =
                strtol(kOmitFrameNumData, &endptr, 10);
        if (*endptr != '\0') {
            ALOGE("%s: Invalid camera omit num. got %s.", __func__, kOmitFrameNumData);
        }
        omit_index++;

        if (omit_index >= OMIT_RESOLUTION_NUM) break;
    }

    // store parsed camera metadata
    camera->camera_metadata_vec[camera_id] = (static_meta[cam_index]);
    ALOGI("%s: camera->camera_metadata_vec size %lu, camera_id %d, cam_index %d", __func__,
          camera->camera_metadata_vec.size(), camera_id, cam_index);

    return true;
}
} // namespace

#define PROP_CAMERA_LAYOUT "ro.boot.camera.layout"
#define PROP_ISPSENSOR_MAXSIZE "ro.boot.camera.ispsensor.maxsize"
bool CameraConfigurationParser::Init() {
    std::string config;
    char name[PATH_MAX] = {0};

    char layout[PROPERTY_VALUE_MAX] = {0};
    property_get(PROP_CAMERA_LAYOUT, layout, "");

    char maxsize[PROPERTY_VALUE_MAX] = {0};
    property_get(PROP_ISPSENSOR_MAXSIZE, maxsize, "");

    if (strcmp(layout, "") == 0)
        snprintf(name, PATH_MAX, "%s_%s%s", kCameraConfiguration,
                 android::base::GetProperty(kSocType, "").c_str(), ".json");
    else {
        if (strcmp(maxsize, "4k") == 0)
            snprintf(name, PATH_MAX, "%s_%s-4k-%s%s", kCameraConfiguration,
                     android::base::GetProperty(kSocType, "").c_str(), layout, ".json");
        else
            snprintf(name, PATH_MAX, "%s_%s-%s%s", kCameraConfiguration,
                     android::base::GetProperty(kSocType, "").c_str(), layout, ".json");
    }

    ALOGI("%s: parse %s", __func__, name);

    std::vector<const char*> configurationFileLocation;
    configurationFileLocation.emplace_back(name);
    mcamera_.camera_metadata_vec.resize(MAX_BASIC_CAMERA_NUM);

    if (!android::base::ReadFileToString(name, &config)) {
        ALOGE("%s: Could not open configuration file: %s", __func__, name);
        return false;
    }

    Json::Reader config_reader;
    Json::Value root;
    if (!config_reader.parse(config, root)) {
        ALOGE("Could not parse configuration file: %s",
              config_reader.getFormattedErrorMessages().c_str());
        return false;
    }

    return ConfigureCameras(root, &mcamera_);
}

} // namespace cameraconfigparser
