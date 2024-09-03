/*
 *  Copyright 2024 NXP.
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

#ifndef _ISP_WRAPPER_H
#define _ISP_WRAPPER_H

#include "CameraUtils.h"
#include "libcamera/camera.h"
#include "libcamera/camera_manager.h"
#include "libcamera/control_ids.h"
#include "libcamera/formats.h"
#include "libcamera/framebuffer.h"
#include "libcamera/pixel_format.h"
#include "libcamera/request.h"

// The id and key values of libcamera::controls can be found in
// libcamera/prebuilt-android/include/libcamera/control_ids.h For a detailed description, refer
// libcamera/src/libcamera/control_ids_core.yaml

typedef struct CamEngineWbGains_s {
    float Red;
    float Blue;
} WbGains;

namespace android {

using cameraconfigparser::CameraSensorMetadata;
using google_camera_hal::HalCameraMetadata;

class ISPWrapper {
public:
    ISPWrapper();
    ~ISPWrapper();
    int process(HalCameraMetadata *pMeta, libcamera::ControlList &controls);
    int processAWB(uint8_t mode, libcamera::ControlList &controls, bool force = false);
    int processAeMode(uint8_t mode, libcamera::ControlList &controls, bool force = false);
    int processExposureGain(int32_t comp, libcamera::ControlList &controls, bool force = false);
    int processExposureTime(int64_t exposureNs, libcamera::ControlList &controls,
                            bool force = false);

private:
    int enableAWB(bool enable, libcamera::ControlList &controls);

private:
    uint8_t m_awb_mode;
    uint8_t m_ae_mode;
    int32_t m_exposure_gain;
    int64_t m_exposure_time;

    float m_ec_gain_min;
    float m_ec_gain_max;
    float m_brightness_min = -1.0;
    float m_brightness_max = 1.0;

    float m_last_exposure_gain;
    int64_t m_last_exposure_time;
};

} // namespace android

#endif // _ISP_WRAPPER_H
