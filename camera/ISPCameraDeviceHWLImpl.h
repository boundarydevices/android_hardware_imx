/*
 *  Copyright 2020-2023 NXP.
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

#ifndef ISP_CAMERA_DEVICE_HWL_IMPL_H
#define ISP_CAMERA_DEVICE_HWL_IMPL_H

#include "CameraDeviceHWLImpl.h"
#include "ISPWrapper.h"
#include "MMAPStream.h"

namespace android {

class ISPCameraDeviceHwlImpl : public CameraDeviceHwlImpl {
public:
    ISPCameraDeviceHwlImpl(
            uint32_t camera_id,
            /*std::vector<char*> devPath,*/ std::vector<std::shared_ptr<char *>> devPaths,
            std::vector<uint32_t> physicalIds, ImxEngine cam_copy_hw, ImxEngine cam_csc_hw,
            const char *hw_jpeg, int use_cpu_encoder, CameraSensorMetadata *cam_metadata,
            PhysicalDeviceMapPtr physical_devices, HwlCameraProviderCallback &callback)
          : CameraDeviceHwlImpl(camera_id, std::move(devPaths), std::move(physicalIds), cam_copy_hw,
                                cam_csc_hw, hw_jpeg, use_cpu_encoder, cam_metadata,
                                std::move(physical_devices), callback) {}

private:
    virtual status_t initSensorStaticData();
};

class ISPCameraMMAPStream : public MMAPStream {
public:
    ISPCameraMMAPStream(CameraDeviceSessionHwlImpl *pSession) : MMAPStream(pSession) {}
    int32_t createISPWrapper(char *pDevPath, CameraSensorMetadata *pSensorData);

    virtual int32_t ISPProcess(void *pMeta, uint32_t format);
    virtual int32_t onDeviceStartLocked();
    virtual int32_t onDeviceStopLocked();
    virtual int32_t onDeviceConfigureLocked(uint32_t format, uint32_t width, uint32_t height,
                                            uint32_t fps);
    virtual int32_t onPrepareLocked(uint32_t format, uint8_t sceneMode);
    std::unique_ptr<ISPWrapper> &getIspWrapper() { return m_IspWrapper; }

private:
    std::unique_ptr<ISPWrapper> m_IspWrapper;
};

} // namespace android

#endif // ISP_CAMERA_DEVICE_HWL_IMPL_H
