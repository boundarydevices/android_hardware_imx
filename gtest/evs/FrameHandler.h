/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef EVS_VTS_FRAMEHANDLER_H
#define EVS_VTS_FRAMEHANDLER_H

#include <android/hardware/automotive/evs/1.0/IEvsDisplay.h>
#include <android/hardware/automotive/evs/1.1/IEvsCamera.h>
#include <android/hardware/automotive/evs/1.1/IEvsCameraStream.h>

#include <queue>

using namespace ::android::hardware::automotive::evs::V1_1;
using ::android::sp;
using ::android::hardware::hidl_handle;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::automotive::evs::V1_0::EvsResult;
using ::android::hardware::automotive::evs::V1_0::IEvsDisplay;
using BufferDesc_1_0 = ::android::hardware::automotive::evs::V1_0::BufferDesc;
using BufferDesc_1_1 = ::android::hardware::automotive::evs::V1_1::BufferDesc;

/*
 * FrameHandler:
 * This class can be used to receive camera imagery from an IEvsCamera implementation.  Given an
 * IEvsDisplay instance at startup, it will forward the received imagery to the display,
 * providing a trivial implementation of a rear vew camera type application.
 * Note that the video frames are delivered on a background thread, while the control interface
 * is actuated from the applications foreground thread.
 */
class FrameHandler : public IEvsCameraStream {
public:
    enum BufferControlFlag {
        eAutoReturn,
        eNoAutoReturn,
    };

    FrameHandler(android::sp<IEvsCamera> pCamera, CameraDesc cameraInfo,
                 android::sp<IEvsDisplay> pDisplay = nullptr, BufferControlFlag mode = eAutoReturn);
    virtual ~FrameHandler() {
        if (mCamera != nullptr) {
            /* shutdown a camera explicitly */
            shutdown();
        }
    }

    void shutdown();

    bool startStream();
    void asyncStopStream();
    void blockingStopStream();

    bool returnHeldBuffer();

    bool isRunning();

    void waitForFrameCount(unsigned frameCount);
    bool waitForEvent(const EvsEventDesc& aTargetEvent, EvsEventDesc& aReceivedEvent,
                      bool ignorePayload = false);
    void getFramesCounters(unsigned* received, unsigned* displayed);
    void getFrameDimension(unsigned* width, unsigned* height);

private:
    // Implementation for ::android::hardware::automotive::evs::V1_0::IEvsCameraStream
    Return<void> deliverFrame(const BufferDesc_1_0& buffer) override;

    // Implementation for ::android::hardware::automotive::evs::V1_1::IEvsCameraStream
    Return<void> deliverFrame_1_1(const hidl_vec<BufferDesc_1_1>& buffer) override;
    Return<void> notify(const EvsEventDesc& event) override;
    void dumpCameraBuffer(const BufferDesc_1_1& buffer);

    // Local implementation details
    bool copyBufferContents(const BufferDesc_1_0& tgtBuffer, const BufferDesc_1_1& srcBuffer);
    const char* eventToString(const EvsEventType aType);

    // Values initialized as startup
    android::sp<IEvsCamera> mCamera;
    CameraDesc mCameraInfo;
    android::sp<IEvsDisplay> mDisplay;
    BufferControlFlag mReturnMode;

    // Since we get frames delivered to us asynchronously via the IEvsCameraStream interface,
    // we need to protect all member variables that may be modified while we're streaming
    // (ie: those below)
    std::mutex mLock;
    std::mutex mEventLock;
    std::condition_variable mEventSignal;
    std::condition_variable mFrameSignal;
    std::queue<hidl_vec<BufferDesc_1_1>> mHeldBuffers;

    bool mRunning = false;
    unsigned mFramesReceived = 0;  // Simple counter -- rolls over eventually!
    unsigned mFramesDisplayed = 0; // Simple counter -- rolls over eventually!
    unsigned mFrameWidth = 0;
    unsigned mFrameHeight = 0;
    EvsEventDesc mLatestEventDesc;
};

#endif // EVS_VTS_FRAMEHANDLER_H
