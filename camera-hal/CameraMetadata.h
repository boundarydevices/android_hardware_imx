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

#ifndef CAMERA_METADATA_H
#define CAMERA_METADATA_H

#include <hal_types.h>
#include <inttypes.h>
#include <log/log.h>
#include <stdint.h>
#include <system/camera.h>
#include <system/graphics.h>

#include <string>

#include "CameraConfigurationParser.h"
#include "hal_camera_metadata.h"

namespace android {

class CameraDeviceHwlImpl;

using google_camera_hal::HalCameraMetadata;
using google_camera_hal::RequestTemplate;
using namespace cameraconfigparser;
class CameraMetadata {
public:
    CameraMetadata() {}
    CameraMetadata(HalCameraMetadata *request_meta) { m_request_meta = request_meta; }

public:
    status_t createMetadata(CameraDeviceHwlImpl *pDev, CameraSensorMetadata mSensorData);
    HalCameraMetadata *GetStaticMeta();

    CameraMetadata *Clone();

    // Get a key's value by tag
    status_t Get(uint32_t tag, camera_metadata_ro_entry *entry) const;

    status_t getRequestSettings(RequestTemplate type,
                                std::unique_ptr<HalCameraMetadata> *default_settings);

    status_t setTemplate(CameraSensorMetadata mSensorData);

    int32_t getGpsCoordinates(double *pCoords, int count);
    int32_t getGpsTimeStamp(int64_t &timeStamp);
    int32_t getGpsProcessingMethod(uint8_t *src, int count);
    int32_t getFocalLength(float &focalLength);
    int32_t getJpegRotation(int32_t &jpegRotation);
    int32_t getJpegQuality(int32_t &quality);
    int32_t getJpegThumbQuality(int32_t &thumb);
    int32_t getJpegThumbSize(int &width, int &height);
    int32_t getMaxJpegSize(int &size);

private:
    status_t createSettingTemplate(std::unique_ptr<HalCameraMetadata> &base, RequestTemplate type,
                                   CameraSensorMetadata mSensorData);

    status_t MergeAndSetMeta(uint32_t tag, int32_t *array_keys_basic, int basic_size,
                             int *array_keys_isp, int isp_size);

private:
    std::unique_ptr<HalCameraMetadata> m_static_meta;
    std::unique_ptr<HalCameraMetadata> m_template_meta[(uint32_t)RequestTemplate::kManual + 1];
    HalCameraMetadata *m_request_meta = nullptr; // meta from framework

    mutable std::mutex metadata_lock_;
};

} // namespace android
#endif
