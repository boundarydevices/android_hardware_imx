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
#ifndef HALS_CAMERACAMERACONFIGURATIONPARSER_H_
#define HALS_CAMERACAMERACONFIGURATIONPARSER_H_

#include <vector>
#include <string>

namespace cameraconfigparser {

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

// Camera properties and features.
struct CameraSensorMetadata {
  // Camera recognized HAL versions.
  enum BufferType { kMmap, kDma };

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
  char camera_type[32];

  // camera node name
  char camera_name[32];

  // max pixel size
  int maxjpegsize;

  // max fps and min fps. the value is set 1/30s ~ 0.3s
  long minframeduration;
  long maxframeduration;

  struct OmitFrame omit_frame[OMIT_RESOLUTION_NUM];
  BufferType buffer_type;
};

struct CameraDefinition {
  HalVersion hal_version;
  CscHw cam_blit_copy_hw;
  CscHw cam_blit_csc_hw;
  std::string jpeg_hw;
  struct CameraSensorMetadata camera_metadata[2];
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
