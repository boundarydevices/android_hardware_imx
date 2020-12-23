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

#ifndef ISP_CAMERA_DEVICE_HWL_IMPL_H
#define ISP_CAMERA_DEVICE_HWL_IMPL_H

#include "MMAPStream.h"
#include "CameraDeviceHWLImpl.h"
#include "ISPWrapper.h"

namespace android {

class ISPCameraDeviceHwlImpl : public CameraDeviceHwlImpl
{
private:
    virtual status_t initSensorStaticData();
};

class ISPCameraMMAPStream : public MMAPStream
{
public:
    ISPCameraMMAPStream(CameraDeviceSessionHwlImpl *pSession) : MMAPStream(pSession) {}
    int32_t createISPWrapper(char *pDevPath, CameraSensorMetadata *pSensorData);

    virtual int32_t ISPProcess(void *pMeta);
    virtual int32_t onDeviceStartLocked();
    virtual int32_t onDeviceConfigureLocked(uint32_t format, uint32_t width, uint32_t height, uint32_t fps);

private:
    std::unique_ptr<ISPWrapper> m_IspWrapper;
};

}  // namespace android

#endif  // ISP_CAMERA_DEVICE_HWL_IMPL_H
