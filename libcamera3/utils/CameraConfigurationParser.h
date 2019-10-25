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

namespace cameraconfigparser {

enum HalVersion { kHalV1, kHalV2, kHalV3 };
// Camera properties and features.
struct CameraDefinition {
  // Camera recognized HAL versions.
  enum BufferType { kMmap, kDma };

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

  // max pixel size
  int maxjpegsize;

  // max fps and min fps. the value is set 1/30s ~ 0.3s
  long minframeduration;
  long maxframeduration;

  BufferType buffer_type;
};

class CameraConfigurationParser {
 public:
  CameraConfigurationParser() {}
  ~CameraConfigurationParser() {}

  CameraDefinition& mcamera()  { return mcamera_; }

  bool Init(const std::string);

 private:
  CameraDefinition mcamera_;
};

}  // namespace cameraconfigparser

#endif  // HALS_CAMERACAMERACONFIGURATIONPARSER_H_
