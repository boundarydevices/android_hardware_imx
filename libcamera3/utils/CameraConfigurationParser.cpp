/*
 * Copyright 2019 NXP
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

//  "ov5640_metadata": [
//    {
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

// Location of the camera json configuration files.
const char* const kCameraConfiguration = "/vendor/etc/config/camera_config";

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


HalVersion ValueToCameraHalVersion(const std::string& value) {
  int temp;
  char* endptr;
  HalVersion hal_version = kHalV1;

  temp = strtol(value.c_str(), &endptr, 10);
  if (endptr != value.c_str() + value.size()) {
    ALOGE("%s: Invalid camera HAL version. Expected number, got %s.",
          __FUNCTION__, value.c_str());
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
            __FUNCTION__, temp);
  }

  return hal_version;
}

// Convert string value to buffer map type
bool ValueToCameraBufferType(const std::string& value,
                              CameraDefinition* camera) {
    if (value == "mmap") {
        camera->buffer_type = CameraDefinition::kMmap;
        return true;
    } else if (value == "dma") {
        camera->buffer_type = CameraDefinition::kDma;
        return true;
    }
    return false;
}

// Process camera definitions.
// Returns true, if definitions were sane.
bool ConfigureCameras(const Json::Value& value,
                      CameraDefinition* camera, const std::string camera_metadata) {
  char* endptr;
  if (!value.isObject()) {
    ALOGE("%s: Configuration root is not an object", __FUNCTION__);
    return false;
  }

  if (!value.isMember(kCameraDefinitionHalVersionKey)) return true;
  if (ValueToCameraHalVersion(
    value[kCameraDefinitionHalVersionKey].asString()) != kHalV3) {
    ALOGE("%s: Invalid camera hal version: key %s is not matched.", __FUNCTION__,
          kCameraDefinitionHalVersionKey);
    return false;
  }

  if (!value.isMember(camera_metadata.c_str())) return true;
  for (Json::ValueConstIterator iter = value[camera_metadata.c_str()].begin();
       iter != value[camera_metadata.c_str()].end(); ++iter) {
      if (!ValueToCameraBufferType(
              (*iter)[kCameraBufferType].asString(),
              camera))
        return false;

      camera->activearraywidth = strtol((*iter)[kActiveArrayWidthKey].asString().c_str(),
                                                        &endptr, 10);
      if (endptr != (*iter)[kActiveArrayWidthKey].asString().c_str() +
          (*iter)[kActiveArrayWidthKey].asString().size()) {
          ALOGE("%s: Invalid camera resolution width. Expected number, got %s.",
               __FUNCTION__, (*iter)[kActiveArrayWidthKey].asString().c_str());
      }

      camera->activearrayheight = strtol((*iter)[kActiveArrayHeightKey].asString().c_str(),
                                                        &endptr, 10);
      if (endptr != (*iter)[kActiveArrayHeightKey].asString().c_str() +
          (*iter)[kActiveArrayHeightKey].asString().size()) {
          ALOGE("%s: Invalid camera ActiveArrayHeight. got %s.",
               __FUNCTION__, (*iter)[kActiveArrayHeightKey].asString().c_str());
      }

      camera->pixelarraywidth = strtol((*iter)[kPixelArrayWidthKey].asString().c_str(),
                                                        &endptr, 10);
      if (endptr != (*iter)[kPixelArrayWidthKey].asString().c_str() +
          (*iter)[kPixelArrayWidthKey].asString().size()) {
          ALOGE("%s: Invalid camera PixelArrayWidth. got %s.",
               __FUNCTION__, (*iter)[kPixelArrayWidthKey].asString().c_str());
      }

      camera->pixelarrayheight = strtol((*iter)[kPixelArrayHeightKey].asString().c_str(),
                                                        &endptr, 10);
      if (endptr != (*iter)[kPixelArrayHeightKey].asString().c_str() +
          (*iter)[kPixelArrayHeightKey].asString().size()) {
          ALOGE("%s: Invalid camera PixelArrayHeight. got %s.",
               __FUNCTION__, (*iter)[kPixelArrayHeightKey].asString().c_str());
      }

      camera->maxjpegsize = strtol((*iter)[kMaxJpegSizeKey].asString().c_str(),
                                                        &endptr, 10);
      if (endptr != (*iter)[kMaxJpegSizeKey].asString().c_str() +
          (*iter)[kMaxJpegSizeKey].asString().size()) {
          ALOGE("%s: Invalid camera MaxJpegSize. got %s.",
               __FUNCTION__, (*iter)[kMaxJpegSizeKey].asString().c_str());
      }

      camera->minframeduration = strtol((*iter)[kMinFrameDurationKey].asString().c_str(),
                                                        &endptr, 10);
      if (endptr != (*iter)[kMinFrameDurationKey].asString().c_str() +
          (*iter)[kMinFrameDurationKey].asString().size()) {
          ALOGE("%s: Invalid camera MinFrameDuration. got %s.",
               __FUNCTION__, (*iter)[kMinFrameDurationKey].asString().c_str());
      }


      camera->maxframeduration = strtol((*iter)[kMaxFrameDurationKey].asString().c_str(),
                                                        &endptr, 10);
      if (endptr != (*iter)[kMaxFrameDurationKey].asString().c_str() +
          (*iter)[kMaxFrameDurationKey].asString().size()) {
          ALOGE("%s: Invalid camera MaxFrameDuration. got %s.",
               __FUNCTION__, (*iter)[kMaxFrameDurationKey].asString().c_str());
      }

      camera->physicalwidth = strtof((*iter)[kPhysicalWidthKey].asString().c_str(),
                                                        &endptr);
      if (endptr != (*iter)[kPhysicalWidthKey].asString().c_str() +
          (*iter)[kPhysicalWidthKey].asString().size()) {
          ALOGE("%s: Invalid camera PhysicalWidth. got %s.",
               __FUNCTION__, (*iter)[kPhysicalWidthKey].asString().c_str());
      }

      camera->physicalheight = strtof((*iter)[kPhysicalHeightKey].asString().c_str(),
                                                        &endptr);
      if (endptr != (*iter)[kPhysicalHeightKey].asString().c_str() +
          (*iter)[kPhysicalHeightKey].asString().size()) {
          ALOGE("%s: Invalid PhysicalHeight. got %s.",
               __FUNCTION__, (*iter)[kPhysicalHeightKey].asString().c_str());
      }

      camera->focallength = strtof((*iter)[kFocalLengthKey].asString().c_str(),
                                                        &endptr);
      if (endptr != (*iter)[kFocalLengthKey].asString().c_str() +
          (*iter)[kFocalLengthKey].asString().size()) {
          ALOGE("%s: Invalid camera FocalLength. got %s.",
               __FUNCTION__, (*iter)[kFocalLengthKey].asString().c_str());
      }

  }

  return true;
}
}  // namespace

bool CameraConfigurationParser::Init(const std::string camera_type) {
  std::string config;
  char name[PATH_MAX] = {0};
  snprintf(name, PATH_MAX, "%s_%s%s", kCameraConfiguration, android::base::GetProperty(kSocType, "").c_str(), ".json");

  if (!android::base::ReadFileToString(name, &config)) {
    ALOGE("%s: Could not open configuration file: %s", __FUNCTION__,
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

  return ConfigureCameras(root, &mcamera_, camera_type);
}

}  // namespace cameraconfigparser
