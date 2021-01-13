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

#ifndef CAMERA_DEVICE_HWL_IMPL_H
#define CAMERA_DEVICE_HWL_IMPL_H

#include <ui/PixelFormat.h>
#include "CameraUtils.h"
#include <camera_device_hwl.h>
#include <hal_types.h>
#include "CameraMetadata.h"
#include "CameraConfigurationParser.h"


namespace android {

using google_camera_hal::CameraBufferAllocatorHwl;
using google_camera_hal::CameraDeviceHwl;
using google_camera_hal::CameraDeviceSessionHwl;
using google_camera_hal::CameraResourceCost;
using google_camera_hal::HalCameraMetadata;
using google_camera_hal::StreamConfiguration;
using google_camera_hal::TorchMode;
using google_camera_hal::HwlCameraProviderCallback;
using google_camera_hal::TorchModeStatus;

using namespace cameraconfigparser;

#define JPEG_HW_NAME_LEN 32

class CameraDeviceHwlImpl : public CameraDeviceHwl
{
public:
    static std::unique_ptr<CameraDeviceHwl> Create(uint32_t camera_id, const char* devPath,
        CscHw cam_copy_hw, CscHw cam_csc_hw, const char *hw_jpeg, CameraSensorMetadata *cam_metadata, HwlCameraProviderCallback callback);

    virtual ~CameraDeviceHwlImpl();

    uint32_t GetCameraId() const
    {
        return camera_id_;
    }

    // Override functions in CameraDeviceHwl.
    status_t GetResourceCost(CameraResourceCost* cost) const override
    {
        cost->resource_cost = 100;
        return OK;
    }

    status_t GetCameraCharacteristics(
        std::unique_ptr<HalCameraMetadata>* characteristics) const override;

    status_t GetPhysicalCameraCharacteristics(
        uint32_t physical_camera_id __unused,
        std::unique_ptr<HalCameraMetadata>* characteristics __unused) const override
    {
        return INVALID_OPERATION;
    }

    status_t SetTorchMode(TorchMode mode) override;

    status_t DumpState(int fd) override;

    status_t CreateCameraDeviceSessionHwl(
        CameraBufferAllocatorHwl* camera_allocator_hwl,
        std::unique_ptr<CameraDeviceSessionHwl>* session) override;

    bool IsStreamCombinationSupported(const StreamConfiguration& stream_config) override;

    // End of override functions in CameraDeviceHwl.

    char *getHwEncoder() { return mJpegHw; }

    CameraSensorMetadata* getSensorData() { return &mSensorData; }

    static bool StreamCombJudge(const StreamConfiguration& stream_config,
        int *pPreviewResolutions, int nPreviewResolutionCount, int *pPictureResolutions, int nPictureResolutionCount);

protected:
    CameraDeviceHwlImpl(uint32_t camera_id, const char *devPath,
        CscHw cam_copy_hw, CscHw cam_csc_hw, const char *hw_jpeg, CameraSensorMetadata *cam_metadata, HwlCameraProviderCallback callback);

private:
    virtual status_t Initialize();
    virtual status_t initSensorStaticData();
    static bool FoundResoulution(int width, int height, int *resArray, int size);

protected:
    status_t setMaxPictureResolutions();
    status_t adjustPreviewResolutions();

private:
    uint32_t camera_id_;

  //  std::unique_ptr<HalCameraMetadata> static_metadata_;
    CameraMetadata *m_meta;
    HwlCameraProviderCallback mCallback;

public:
    int mPreviewResolutions[MAX_RESOLUTION_SIZE];
    int mPreviewResolutionCount;
    int mPictureResolutions[MAX_RESOLUTION_SIZE];
    int mPictureResolutionCount;
    int mAvailableFormats[MAX_SENSOR_FORMAT];
    int mAvailableFormatCount;

    int mTargetFpsRange[MAX_FPS_RANGE];
    int mMaxWidth;
    int mMaxHeight;

     // preview and picture format.
    PixelFormat mPicturePixelFormat;
    PixelFormat mPreviewPixelFormat;

    // vpu and capture limitation.
    int mVpuSupportFmt[MAX_VPU_SUPPORT_FORMAT];
    int mPictureSupportFmt[MAX_PICTURE_SUPPORT_FORMAT];

    int mSensorFormats[MAX_SENSOR_FORMAT];
    int mSensorFormatCount;
    char mDevPath[CAMAERA_FILENAME_LENGTH];


    CscHw mCamBlitCopyType;
    CscHw mCamBlitCscType;
    char mJpegHw[JPEG_HW_NAME_LEN] = { 0 };
    CameraSensorMetadata mSensorData;
};

}  // namespace android

#endif  // CAMERA_DEVICE_HWL_IMPL_H
