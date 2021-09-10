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
#ifndef HALS_CAMERACAMERACONFIGURATIONPARSER_H_
#define HALS_CAMERACAMERACONFIGURATIONPARSER_H_

#include <vector>
#include <string>
#include "hal_types.h"
namespace cameraconfigparser {
using android::google_camera_hal::CameraDeviceStatus;
using android::google_camera_hal::HalCameraMetadata;

enum CameraId {
  BACK_CAM_ID = 0,
  FRONT_CAM_ID,
  NUM_CAM_ID,
};

enum CscHw {
  GPU_2D,
  GPU_3D,
  DPU,
  PXP,
  IPU,
  CPU,
};

struct OmitFrame {
  int width;
  int height;
  int omitnum;
};

enum HalVersion { kHalV1, kHalV2, kHalV3 };

#define OMIT_RESOLUTION_NUM 8
#define META_STRING_SIZE 32
#define MAX_BASIC_CAMERA_NUM 24

// Camera properties and features.
struct CameraSensorMetadata {
  // Camera recognized HAL versions.
  enum BufferType { kMmap, kDma };

  int mplane;

  int orientation;
  // the max active pixel width for camera sensor
  int activearraywidth;
  // the max active pixel height for camera sensor
  int activearrayheight;

  // the max pixel for camera sensor
  int pixelarraywidth;
  // the max pixel for camera sensor
  int pixelarrayheight;

  // max_pixel_width * physical size of one pixel
  // physical size of one pixel for ov5640 is 1.4um
  // physical size of one pixel for max9286 is 4.2um
  float physicalwidth;
  // max_pixel_height * physical size of one pixel
  float physicalheight;

  // focal size
  float focallength;

  // "back" or "front"
  char camera_type[META_STRING_SIZE];

  // camera node name
  char camera_name[META_STRING_SIZE];

  // device node name, In some case, need use the given node.
  char device_node[META_STRING_SIZE];

  // bus info, in some case, need use it to choose the v4l2 device
  char bus_info[META_STRING_SIZE];

  // max pixel size
  int maxjpegsize;

  // max fps and min fps. the value is set 1/30s ~ 0.3s
  long minframeduration;
  long maxframeduration;

  struct OmitFrame omit_frame[OMIT_RESOLUTION_NUM];
  BufferType buffer_type;

  // Ref https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#CONTROL_AE_COMPENSATION_RANGE
  // and https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#CONTROL_AE_COMPENSATION_STEP.
  // In short, EV(exposure value) = AeComp * AeCompStepNumerator / AeCompStepDenominator.
  // Ref above link, One unit of EV compensation changes the brightness of the captured image by a factor of two.
  // +1 EV doubles the image brightness, while -1 EV halves the image brightness.
  int mAeCompMin;
  int mAeCompMax;
  int mAeCompStepNumerator;
  int mAeCompStepDenominator;
  int64_t mExposureNsMin;
  int64_t mExposureNsMax;
  int mAvailableCapabilities;
  int mMaxWidth;
  int mMaxHeight;
};

typedef std::unordered_map<uint32_t, std::pair<CameraDeviceStatus, std::unique_ptr<CameraSensorMetadata>>> PhysicalDeviceMap;
typedef std::unique_ptr<PhysicalDeviceMap> PhysicalDeviceMapPtr;

typedef std::unordered_map<uint32_t, std::unique_ptr<HalCameraMetadata>> PhysicalMetaMap;
typedef std::unique_ptr<PhysicalMetaMap> PhysicalMetaMapPtr;

struct CameraDefinition {
  HalVersion hal_version;
  CscHw cam_blit_copy_hw;
  CscHw cam_blit_csc_hw;
  std::string jpeg_hw;
  std::vector<CameraSensorMetadata> camera_metadata_vec;
  std::unordered_map<uint32_t, std::vector<std::pair<CameraDeviceStatus, uint32_t>>> camera_id_map_;
};

class CameraConfigurationParser {
  public:
    CameraConfigurationParser() {}
    ~CameraConfigurationParser() {}

    CameraDefinition& mcamera()  { return mcamera_; }

    bool Init();

  private:
    CameraDefinition mcamera_;
};

}  // namespace cameraconfigparser

#endif  // HALS_CAMERACAMERACONFIGURATIONPARSER_H_
