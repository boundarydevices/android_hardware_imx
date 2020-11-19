/*
 * Copyright (C) 2016 The Android Open Source Project
 * Copyright 2019 NXP.
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

#ifndef _FSL_EVS_CAMERA_H
#define _FSL_EVS_CAMERA_H

#include <hwbinder/IBinder.h>
#include <android/hardware/automotive/evs/1.1/types.h>
#include <android/hardware/automotive/evs/1.1/IEvsCamera.h>
#include <android/hardware/automotive/evs/1.1/IEvsCameraStream.h>
#include <android/hardware/automotive/evs/1.1/IEvsDisplay.h>
#include <android/hardware/camera/device/3.2/ICameraDevice.h>

#include <stdio.h>
#include <stdlib.h>
#include <set>
#include <thread>
#include <functional>

#include <Memory.h>
#include <MemoryDesc.h>
#include <MemoryManager.h>
#include <unordered_map>
#include <unordered_set>
#include <linux/videodev2.h>
#include <system/camera_metadata.h>
#include "ConfigManager.h"

using ::android::hardware::hidl_string;
using ::android::hardware::camera::device::V3_2::Stream;
using ::android::hardware::automotive::evs::V1_0::EvsResult;
using ::android::hardware::automotive::evs::V1_0::CameraDesc;
using BufferDesc_1_0       = ::android::hardware::automotive::evs::V1_0::BufferDesc;
using BufferDesc_1_1       = ::android::hardware::automotive::evs::V1_1::BufferDesc;
using IEvsDisplay_1_0      = ::android::hardware::automotive::evs::V1_0::IEvsDisplay;
using IEvsDisplay_1_1      = ::android::hardware::automotive::evs::V1_1::IEvsDisplay;
using IEvsCameraStream_1_0 = ::android::hardware::automotive::evs::V1_0::IEvsCameraStream;
using IEvsCameraStream_1_1 = ::android::hardware::automotive::evs::V1_1::IEvsCameraStream;

#define EVS_FAKE_PROP   "vendor.evs.fake.enable"
struct forwardframe {
    fsl::Memory *buf;
    int index;
    std::string deviceid;
};

using ::android::hardware::hidl_death_recipient;
namespace android {
namespace hardware {
namespace automotive {
namespace evs {
namespace V1_1 {
namespace implementation {

#define CAMERA_BUFFER_NUM 3

// MAX_BUFFERS_IN_FLIGHT mean the max value of mFramesAllowed
// currently v4l2 driver allocate 10 buffers now, so the max buffer is 10
#define MAX_BUFFERS_IN_FLIGHT  10

class EvsCamera : public IEvsCamera
{
public:
    // Methods from ::android::hardware::automotive::evs::V1_0::IEvsCamera follow.
    Return<void> getCameraInfo(getCameraInfo_cb _hidl_cb)  override;
    Return<void>      getCameraInfo_1_1(getCameraInfo_1_1_cb _hidl_cb)  override;
    Return<void>      getPhysicalCameraInfo(const hidl_string& deviceId,
                           getPhysicalCameraInfo_cb _hidl_cb)  override;
    Return <EvsResult> setMaxFramesInFlight(uint32_t bufferCount) override;
    Return <EvsResult> startVideoStream(const ::android::sp<IEvsCameraStream_1_0>& stream) override;
    Return<void> doneWithFrame(const BufferDesc_1_0& buffer) override;
    Return<EvsResult> doneWithFrame_1_1(const hidl_vec<BufferDesc_1_1>& buffer) override;
    bool isLogicalCamera(const camera_metadata_t *metadata);
    std::unordered_set<std::string> getPhysicalCameraInLogic(const camera_metadata_t *metadata);
    Return<void> stopVideoStream() override;
    Return <int32_t> getExtendedInfo(uint32_t opaqueIdentifier) override;
    Return <EvsResult> setExtendedInfo(uint32_t opaqueIdentifier, int32_t opaqueValue) override;
    Return<EvsResult> pauseVideoStream() override;
    Return<EvsResult> resumeVideoStream() override;
    Return<EvsResult> setMaster() override;
    Return<EvsResult> forceMaster(const sp<IEvsDisplay_1_0>&) override;
    Return<EvsResult> unsetMaster() override;
    Return<void>      setIntParameter(CameraParam id, int32_t value,
                                                 setIntParameter_cb _hidl_cb) override;
    Return<void>      getIntParameter(CameraParam id,
                                                 getIntParameter_cb _hidl_cb) override;
    Return<EvsResult> setExtendedInfo_1_1(uint32_t opaqueIdentifier,
                                               const hidl_vec<uint8_t>& opaqueValue) override;
    Return<void>      getExtendedInfo_1_1(uint32_t opaqueIdentifier,
                                               getExtendedInfo_1_1_cb _hidl_cb) override;
    Return<void>      importExternalBuffers(const hidl_vec<BufferDesc_1_1>& buffers,
                                     importExternalBuffers_cb _hidl_cb) override;
    Return<void>      getParameterList(getParameterList_cb _hidl_cb) override;
    Return<void>      getIntParameterRange(CameraParam id,
                                 getIntParameterRange_cb _hidl_cb) override;

    // Implementation details
    EvsCamera(const char *videoName, const camera_metadata_t *metadata);
    std::string getVideoDevice(const std::string videoname);
    virtual ~EvsCamera() override;
    void shutdown();
    void openup(const char *deviceName);

    const CameraDesc& getDesc() { return mDescription; };

protected:
    virtual bool onOpen(const char* deviceName) = 0;
    virtual void onClose() = 0;

    virtual bool onStart() = 0;
    virtual void onStop() = 0;
    virtual int getParameter(v4l2_control& control) = 0;
    virtual int setParameter(v4l2_control& control) = 0;
    virtual std::set<uint32_t>  enumerateCameraControls() = 0;

    virtual bool isOpen() = 0;
    // Valid only after open()
    virtual bool onFrameReturn(int index, std::string deviceid) = 0;
    virtual void onFrameCollect(std::vector<struct forwardframe> &frame) = 0;

    // onIncreaseMemoryBuffer: increase number buffers for every camera
    virtual void onIncreaseMemoryBuffer(unsigned number) = 0;
    // destroy all available buffer
    virtual void onMemoryDestroy() = 0;

private:
    void releaseResource(void);
    class EvsAppRecipient : public hidl_death_recipient
    {
    public:
        EvsAppRecipient(sp<EvsCamera> camera) : mCamera(camera) {}
        ~EvsAppRecipient() {}
        virtual void serviceDied(uint64_t cookie,
              const ::android::wp<::android::hidl::base::V1_0::IBase>& /*who*/);

    private:
        sp<EvsCamera> mCamera;
    };
    sp<EvsAppRecipient> mEvsAppRecipient;
    Return<void> doneWithFrame_impl(const uint32_t id, const buffer_handle_t handle, std::string deviceid);
    inline bool convertToV4l2CID(CameraParam id, uint32_t& v4l2cid);

    // These functions are used to send/receive frame.
    void forwardFrame(std::vector<struct forwardframe> &fwframes);
    void collectFrames();
    std::set<uint32_t> mCameraControls;     // Available camera controls

protected:
    // The callback used to deliver each frame.
    sp <IEvsCameraStream_1_0> mStream = nullptr;
    sp <IEvsCameraStream_1_1> mStream_1_1 = nullptr;
    // The properties of this camera.
    CameraDesc mDescription = {};
    std::unordered_map<std::string, CameraDesc> mLogicDescription;

    // Synchronization deconflict capture thread from main service thread.
    // Note that service interface remains single threaded (ie: not reentrant)
    std::mutex mLock;
    bool mLogiccam;

    fsl::Memory* mBuffers[CAMERA_BUFFER_NUM] = {nullptr};
    std::unordered_map<int, std::vector<fsl::Memory*>> mCamBuffers;

    __u32 mFormat = 0;
    __u32 mWidth  = 0;
    __u32 mHeight = 0;

    int mNumInLogic;

    unsigned mFramesAllowed;
    unsigned mFramesInUse;

    // it will map the index between v4l2 buffer and the grolloc buffer
    std::unordered_map<std::string, std::unordered_map<int, int>> mBufferMap;

    // The thread we'll use to dispatch frames.
    std::thread mCaptureThread;
    // Used to signal the frame loop (see RunModes below).
    std::atomic<int> mRunMode;

    // Careful changing these -- we're using bit-wise ops to manipulate these
    enum RunModes {
        STOPPED     = 0,
        RUN         = 1,
        STOPPING    = 2,
    };
    int mDeqIdx;

    std::unordered_map<uint32_t, std::vector<uint8_t>> mExtInfo;
};

} // namespace implementation
} // namespace V1_0
} // namespace evs
} // namespace automotive
} // namespace hardware
} // namespace android

#endif  // _FSL_EVS_CAMERA_H
