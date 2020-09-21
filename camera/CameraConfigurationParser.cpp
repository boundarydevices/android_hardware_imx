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
#include <android-base/strings.h>
#include <android-base/properties.h>
#include <log/log.h>
#include <json/json.h>
#include <json/reader.h>
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

const char* const kCameraMetadataKey = "camera_metadata";
const char* const kCameraTypeKey = "camera_type";
const char* const kMPlaneKey = "mplane";
const char* const kCameraNameKey = "camera_name";
const char* const kDeviceNodeKey = "device_node";
const char* const kOrientationKey = "orientation";

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
const char* const kOmitFrameKey = "OmitFrame";
const char* const kOmitFrameWidthKey = "width";
const char* const kOmitFrameHeightKey = "height";
const char* const kOmitFrameNumKey = "omit_num";


#define CSC_HW_GPU_2D "GPU_2D"
#define CSC_HW_GPU_3D "GPU_3D"
#define CSC_HW_IPU "IPU"
#define CSC_HW_PXP "PXP"
#define CSC_HW_DPU "DPU"
#define CSC_HW_CPU "CPU"

HalVersion ValueToCameraHalVersion(const std::string& value) {
  int temp;
  char* endptr;
  HalVersion hal_version = kHalV1;

  temp = strtol(value.c_str(), &endptr, 10);
  if (endptr != value.c_str() + value.size()) {
    ALOGE("%s: Invalid camera HAL version. Expected number, got %s.",
          __func__, value.c_str());
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
      ALOGE("%s: Invalid camera HAL version. Version %d not supported.",
            __func__, temp);
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
bool ValueToCameraBufferType(const std::string& value,
                              CameraDefinition* camera,
                              int cam_metadata_type_index) {
    if (value == "mmap") {
        camera->camera_metadata[cam_metadata_type_index].buffer_type = CameraSensorMetadata::kMmap;
        return true;
    } else if (value == "dma") {
        camera->camera_metadata[cam_metadata_type_index].buffer_type = CameraSensorMetadata::kDma;
        return true;
    }
    return false;
}

// Process camera definitions.
// Returns true, if definitions were sane.
bool ConfigureCameras(const Json::Value& value,
                      CameraDefinition* camera) {
  char* endptr;
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

  int cam_index = 0;
  for (Json::ValueConstIterator iter = value[kCameraMetadataKey].begin();
        iter != value[kCameraMetadataKey].end(); ++iter) {

    if ((*iter)[kCameraTypeKey].asString() == "back") {
        cam_index = BACK_CAM_ID;
    } else if ((*iter)[kCameraTypeKey].asString() == "front") {
        cam_index = FRONT_CAM_ID;
    }

    strncpy(camera->camera_metadata[cam_index].camera_name,
              (*iter)[kCameraNameKey].asString().c_str(),
              META_STRING_SIZE);
    camera->camera_metadata[cam_index].camera_name[META_STRING_SIZE - 1] = 0;

    if(iter->isMember(kDeviceNodeKey)) {
        strncpy(camera->camera_metadata[cam_index].device_node,
              (*iter)[kDeviceNodeKey].asString().c_str(),
              META_STRING_SIZE);
        camera->camera_metadata[cam_index].device_node[META_STRING_SIZE - 1] = 0;
    }
    else
        camera->camera_metadata[cam_index].device_node[0] = 0;

    strncpy(camera->camera_metadata[cam_index].camera_type,
              (*iter)[kCameraTypeKey].asString().c_str(),
              META_STRING_SIZE);
    camera->camera_metadata[cam_index].camera_type[META_STRING_SIZE - 1] = 0;

    if (!ValueToCameraBufferType(
              (*iter)[kCameraBufferType].asString(),
              camera, cam_index))
        return false;

    if(iter->isMember(kMPlaneKey)) {
        camera->camera_metadata[cam_index].mplane = strtol((*iter)[kMPlaneKey].asString().c_str(), NULL, 10);
    }
    else
        camera->camera_metadata[cam_index].mplane = 0;

    camera->camera_metadata[cam_index].orientation = strtol((*iter)[kOrientationKey].asString().c_str(),
                                                        &endptr, 10);

    if (endptr != (*iter)[kOrientationKey].asString().c_str() +
          (*iter)[kOrientationKey].asString().size()) {
          ALOGE("%s: Invalid camera orientation. Expected number, got %s.",
               __func__, (*iter)[kActiveArrayWidthKey].asString().c_str());
    }

    camera->camera_metadata[cam_index].activearraywidth = strtol((*iter)[kActiveArrayWidthKey].asString().c_str(),
                                                        &endptr, 10);

    if (endptr != (*iter)[kActiveArrayWidthKey].asString().c_str() +
          (*iter)[kActiveArrayWidthKey].asString().size()) {
          ALOGE("%s: Invalid camera resolution width. Expected number, got %s.",
               __func__, (*iter)[kActiveArrayWidthKey].asString().c_str());
    }

    camera->camera_metadata[cam_index].activearrayheight = strtol((*iter)[kActiveArrayHeightKey].asString().c_str(),
                                                        &endptr, 10);
    if (endptr != (*iter)[kActiveArrayHeightKey].asString().c_str() +
        (*iter)[kActiveArrayHeightKey].asString().size()) {
        ALOGE("%s: Invalid camera ActiveArrayHeight. got %s.",
             __func__, (*iter)[kActiveArrayHeightKey].asString().c_str());
    }

    camera->camera_metadata[cam_index].pixelarraywidth = strtol((*iter)[kPixelArrayWidthKey].asString().c_str(),
                                                        &endptr, 10);
    if (endptr != (*iter)[kPixelArrayWidthKey].asString().c_str() +
        (*iter)[kPixelArrayWidthKey].asString().size()) {
        ALOGE("%s: Invalid camera PixelArrayWidth. got %s.",
             __func__, (*iter)[kPixelArrayWidthKey].asString().c_str());
    }

    camera->camera_metadata[cam_index].pixelarrayheight = strtol((*iter)[kPixelArrayHeightKey].asString().c_str(),
                                                      &endptr, 10);
    if (endptr != (*iter)[kPixelArrayHeightKey].asString().c_str() +
        (*iter)[kPixelArrayHeightKey].asString().size()) {
        ALOGE("%s: Invalid camera PixelArrayHeight. got %s.",
             __func__, (*iter)[kPixelArrayHeightKey].asString().c_str());
    }

    camera->camera_metadata[cam_index].maxjpegsize = strtol((*iter)[kMaxJpegSizeKey].asString().c_str(),
                                                        &endptr, 10);
    if (endptr != (*iter)[kMaxJpegSizeKey].asString().c_str() +
        (*iter)[kMaxJpegSizeKey].asString().size()) {
        ALOGE("%s: Invalid camera MaxJpegSize. got %s.",
             __func__, (*iter)[kMaxJpegSizeKey].asString().c_str());
    }

    camera->camera_metadata[cam_index].minframeduration = strtol((*iter)[kMinFrameDurationKey].asString().c_str(),
                                                        &endptr, 10);
    if (endptr != (*iter)[kMinFrameDurationKey].asString().c_str() +
        (*iter)[kMinFrameDurationKey].asString().size()) {
        ALOGE("%s: Invalid camera MinFrameDuration. got %s.",
             __func__, (*iter)[kMinFrameDurationKey].asString().c_str());
    }


    camera->camera_metadata[cam_index].maxframeduration = strtol((*iter)[kMaxFrameDurationKey].asString().c_str(),
                                                      &endptr, 10);
    if (endptr != (*iter)[kMaxFrameDurationKey].asString().c_str() +
        (*iter)[kMaxFrameDurationKey].asString().size()) {
        ALOGE("%s: Invalid camera MaxFrameDuration. got %s.",
             __func__, (*iter)[kMaxFrameDurationKey].asString().c_str());
    }

    camera->camera_metadata[cam_index].physicalwidth = strtof((*iter)[kPhysicalWidthKey].asString().c_str(),
                                                      &endptr);
    if (endptr != (*iter)[kPhysicalWidthKey].asString().c_str() +
        (*iter)[kPhysicalWidthKey].asString().size()) {
        ALOGE("%s: Invalid camera PhysicalWidth. got %s.",
             __func__, (*iter)[kPhysicalWidthKey].asString().c_str());
    }
    camera->camera_metadata[cam_index].physicalheight = strtof((*iter)[kPhysicalHeightKey].asString().c_str(),
                                                    &endptr);
    if (endptr != (*iter)[kPhysicalHeightKey].asString().c_str() +
        (*iter)[kPhysicalHeightKey].asString().size()) {
        ALOGE("%s: Invalid PhysicalHeight. got %s.",
             __func__, (*iter)[kPhysicalHeightKey].asString().c_str());
    }
    camera->camera_metadata[cam_index].focallength = strtof((*iter)[kFocalLengthKey].asString().c_str(),
                                                      &endptr);
    if (endptr != (*iter)[kFocalLengthKey].asString().c_str() +
        (*iter)[kFocalLengthKey].asString().size()) {
        ALOGE("%s: Invalid camera FocalLength. got %s.",
             __func__, (*iter)[kFocalLengthKey].asString().c_str());
    }

    int omit_index = 0;
    for (Json::ValueConstIterator omititer = (*iter)[kOmitFrameKey].begin();
                 omititer != (*iter)[kOmitFrameKey].end(); ++omititer) {
        camera->camera_metadata[cam_index].omit_frame[omit_index].width = strtol((*omititer)[kOmitFrameWidthKey].asString().c_str(),
                                                                      &endptr, 10);
        if (endptr != (*omititer)[kOmitFrameWidthKey].asString().c_str() +
               (*omititer)[kOmitFrameWidthKey].asString().size()) {
             ALOGE("%s: Invalid camera omit width. Expected number, got %s.",
                   __func__, (*omititer)[kOmitFrameWidthKey].asString().c_str());
        }

        camera->camera_metadata[cam_index].omit_frame[omit_index].height = strtol((*omititer)[kOmitFrameHeightKey].asString().c_str(),
                                                                     &endptr, 10);
        if (endptr != (*omititer)[kOmitFrameHeightKey].asString().c_str() +
               (*omititer)[kOmitFrameHeightKey].asString().size()) {
             ALOGE("%s: Invalid camera omit height. Expected number, got %s.",
                   __func__, (*omititer)[kOmitFrameHeightKey].asString().c_str());
        }

        camera->camera_metadata[cam_index].omit_frame[omit_index].omitnum = strtol((*omititer)[kOmitFrameNumKey].asString().c_str(),
                                                                     &endptr, 10);
        if (endptr != (*omititer)[kOmitFrameNumKey].asString().c_str() +
               (*omititer)[kOmitFrameNumKey].asString().size()) {
             ALOGE("%s: Invalid camera omit num. Expected number, got %s.",
                   __func__, (*omititer)[kOmitFrameNumKey].asString().c_str());
        }
        omit_index++;

        if(omit_index >= OMIT_RESOLUTION_NUM)
            break;
    }
  }

  return true;
}
}  // namespace

bool CameraConfigurationParser::Init() {
  std::string config;
  char name[PATH_MAX] = {0};
  snprintf(name, PATH_MAX, "%s_%s%s", kCameraConfiguration, android::base::GetProperty(kSocType, "").c_str(), ".json");

  if (!android::base::ReadFileToString(name, &config)) {
    ALOGE("%s: Could not open configuration file: %s", __func__,
          name);
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

}  // namespace cameraconfigparser
