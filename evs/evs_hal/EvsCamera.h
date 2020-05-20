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

#include <android/hardware/automotive/evs/1.0/types.h>
#include <android/hardware/automotive/evs/1.0/IEvsCamera.h>
#include <hwbinder/IBinder.h>

#include <thread>
#include <functional>

#include <Memory.h>
#include <MemoryDesc.h>
#include <MemoryManager.h>

namespace android {
namespace hardware {
namespace automotive {
namespace evs {
namespace V1_0 {
namespace implementation {

using ::android::hardware::hidl_death_recipient;

#define CAMERA_BUFFER_NUM 3

class EvsCamera : public IEvsCamera
{
public:
    // Methods from ::android::hardware::automotive::evs::V1_0::IEvsCamera follow.
    Return<void> getCameraInfo(getCameraInfo_cb _hidl_cb)  override;
    Return <EvsResult> setMaxFramesInFlight(uint32_t bufferCount) override;
    Return <EvsResult> startVideoStream(const ::android::sp<IEvsCameraStream>& stream) override;
    Return<void> doneWithFrame(const BufferDesc& buffer) override;
    Return<void> stopVideoStream() override;
    Return <int32_t> getExtendedInfo(uint32_t opaqueIdentifier) override;
    Return <EvsResult> setExtendedInfo(uint32_t opaqueIdentifier, int32_t opaqueValue) override;

    // Implementation details
    EvsCamera(const char *deviceName);
    virtual ~EvsCamera() override;
    void shutdown();
    void openup(const char *deviceName);

    const CameraDesc& getDesc() { return mDescription; };

protected:
    virtual bool onOpen(const char* deviceName) = 0;
    virtual void onClose() = 0;

    virtual bool onStart() = 0;
    virtual void onStop() = 0;

    virtual bool isOpen() = 0;
    // Valid only after open()
    virtual bool onFrameReturn(int index) = 0;
    virtual fsl::Memory* onFrameCollect(int &index) = 0;

    virtual void onMemoryCreate();
    virtual void onMemoryDestroy();

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

    // These functions are used to send/receive frame.
    void forwardFrame(fsl::Memory* handle, int index);
    void collectFrames();

protected:
    // The callback used to deliver each frame.
    sp <IEvsCameraStream> mStream = nullptr;
    // The properties of this camera.
    CameraDesc mDescription = {};

    // Synchronization deconflict capture thread from main service thread.
    // Note that service interface remains single threaded (ie: not reentrant)
    std::mutex mLock;

    fsl::Memory* mBuffers[CAMERA_BUFFER_NUM] = {nullptr};

    __u32 mFormat = 0;
    __u32 mWidth  = 0;
    __u32 mHeight = 0;

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
};

} // namespace implementation
} // namespace V1_0
} // namespace evs
} // namespace automotive
} // namespace hardware
} // namespace android

#endif  // _FSL_EVS_CAMERA_H
