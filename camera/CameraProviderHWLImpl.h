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

#ifndef CAMERA_PROVIDER_HWL_IMPL_H
#define CAMERA_PROVIDER_HWL_IMPL_H

#include <camera_provider_hwl.h>
#include <hal_types.h>
#include <json/json.h>
#include <json/reader.h>
#include <future>

#include "CameraUtils.h"
#include "CameraMetadata.h"
#include "CameraConfigurationParser.h"

namespace android {

using google_camera_hal::CameraBufferAllocatorHwl;
using google_camera_hal::CameraDeviceHwl;
using google_camera_hal::CameraDeviceStatus;
using google_camera_hal::CameraIdAndStreamConfiguration;
using google_camera_hal::CameraProviderHwl;
using google_camera_hal::HalCameraMetadata;
using google_camera_hal::HwlCameraProviderCallback;
using google_camera_hal::HwlPhysicalCameraDeviceStatusChangeFunc;
using google_camera_hal::HwlTorchModeStatusChangeFunc;
using google_camera_hal::VendorTagSection;

using namespace cameraconfigparser;

struct nodeSet {
    char nodeName[CAMERA_SENSOR_LENGTH];
    char devNode[CAMERA_SENSOR_LENGTH];
    char busInfo[CAMERA_SENSOR_LENGTH];
    bool isHeld;
    nodeSet* next;
};

class CameraProviderHwlImpl : public CameraProviderHwl
{
public:
    // Return a unique pointer to CameraProviderHwlImpl. Calling Create()
    // again before the previous one is destroyed will fail.
    static std::unique_ptr<CameraProviderHwlImpl> Create();

    virtual ~CameraProviderHwlImpl();

    // Override functions in CameraProviderHwl.
    status_t SetCallback(const HwlCameraProviderCallback& callback) override;
    status_t TriggerDeferredCallbacks() override;

    status_t GetVendorTags(
        std::vector<VendorTagSection>* vendor_tag_sections) override;

    status_t GetVisibleCameraIds(std::vector<std::uint32_t>* camera_ids) override;

    bool IsSetTorchModeSupported() override
    {
        return true;
    }

    status_t GetConcurrentStreamingCameraIds(
        std::vector<std::unordered_set<uint32_t>>* combinations) override;

    status_t IsConcurrentStreamCombinationSupported(
        const std::vector<CameraIdAndStreamConfiguration>& configs, bool* is_supported) override;

    status_t CreateCameraDeviceHwl(
        uint32_t camera_id,
        std::unique_ptr<CameraDeviceHwl>* camera_device_hwl) override;

    status_t CreateBufferAllocatorHwl(std::unique_ptr<CameraBufferAllocatorHwl>*
                                          camera_buffer_allocator_hwl) override;
    // End of override functions in CameraProviderHwl.

private:
    status_t Initialize();


    void enumSensorSet();
    int32_t matchPropertyName(nodeSet* nodes, int32_t index);
    int32_t matchDevNodes();
    int32_t getNodeName(const char* devNode, char name[], size_t length, char busInfo[], size_t busInfoLen);

private:
//    std::vector<std::unique_ptr<HalCameraMetadata>> static_metadata_;
//    std::vector<CameraMetadata *> m_meta_list;

    HwlCameraProviderCallback mCallback;

    std::mutex status_callback_future_lock_;
    std::future<void> status_callback_future_;

    CameraConfigurationParser mCameraCfgParser;
    CameraDefinition mCameraDef;

    SensorSet mSets[MAX_CAMERAS];
//    int32_t mCameraCount;

    std::vector<std::uint32_t> camera_id_list;
    std::map<uint32_t, CameraDeviceHwl*> device_map;
};

extern "C" CameraProviderHwl* CreateCameraProviderHwl()
{
    auto provider = CameraProviderHwlImpl::Create();
    return provider.release();
}

}  // namespace android

#endif  // CAMERA_PROVIDER_HWL_IMPL_H
