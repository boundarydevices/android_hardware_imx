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

#ifndef ANDROID_HARDWARE_AUTOMOTIVE_EVS_V1_1_EVSCAMERAENUMERATOR_H
#define ANDROID_HARDWARE_AUTOMOTIVE_EVS_V1_1_EVSCAMERAENUMERATOR_H

#include <android/hardware/automotive/evs/1.1/IEvsEnumerator.h>
#include <android/hardware/automotive/evs/1.1/IEvsCamera.h>
#include <android/frameworks/automotive/display/1.0/IAutomotiveDisplayProxyService.h>
#include <android-base/unique_fd.h>
#include <utils/threads.h>
#include "ConfigManager.h"

#include <list>
#include <string>

using EvsDisplayState = ::android::hardware::automotive::evs::V1_0::DisplayState;
using IEvsCamera_1_0  = ::android::hardware::automotive::evs::V1_0::IEvsCamera;
using IEvsCamera_1_1  = ::android::hardware::automotive::evs::V1_1::IEvsCamera;
using IEvsDisplay_1_0  = ::android::hardware::automotive::evs::V1_0::IEvsDisplay;
using IEvsDisplay_1_1  = ::android::hardware::automotive::evs::V1_1::IEvsDisplay;
using ::android::hardware::camera::device::V3_2::Stream;
using IEvsCameraStream_1_0 = ::android::hardware::automotive::evs::V1_0::IEvsCameraStream;
using BufferDesc_1_0       = ::android::hardware::automotive::evs::V1_0::BufferDesc;
using BufferDesc_1_1       = ::android::hardware::automotive::evs::V1_1::BufferDesc;

using CameraDesc_1_1 = ::android::hardware::automotive::evs::V1_1::CameraDesc;
using CameraDesc_1_0 = ::android::hardware::automotive::evs::V1_0::CameraDesc;

namespace android {
namespace hardware {
namespace automotive {
namespace evs {
namespace V1_1 {
namespace implementation {
using android::Thread;
using android::sp;
using android::frameworks::automotive::display::V1_0::IAutomotiveDisplayProxyService;

class EvsCamera;    // from EvsCamera.h
class EvsDisplay;    // from EvsDisplay.h


class EvsEnumerator : public IEvsEnumerator {
public:
    // Methods from ::android::hardware::automotive::evs::V1_0::IEvsEnumerator follow.
    Return<void> getCameraList(getCameraList_cb _hidl_cb)  override;
    Return<sp<IEvsCamera_1_0>> openCamera(const hidl_string& cameraId) override;
    Return<void> closeCamera(const ::android::sp<IEvsCamera_1_0>& carCamera)  override;
    Return<sp<IEvsDisplay_1_0>> openDisplay()  override;
    Return<void> closeDisplay(const ::android::sp<IEvsDisplay_1_0>& display)  override;
    Return<EvsDisplayState> getDisplayState()  override;

    Return<void>                getCameraList_1_1(getCameraList_1_1_cb _hidl_cb) override;
    Return<sp<IEvsCamera_1_1>>  openCamera_1_1(const hidl_string& cameraId,
                                               const Stream& streamCfg) override;
    Return<bool>                isHardware() override { return true; }
    Return<void>                getDisplayIdList(getDisplayIdList_cb _list_cb) override;
    Return<sp<IEvsDisplay_1_1>> openDisplay_1_1(uint8_t port) override;
    Return<sp<IEvsUltrasonicsArray>> openUltrasonicsArray(
              const hidl_string& ultrasonicsArrayId) override;
    Return<void> closeUltrasonicsArray(
              const ::android::sp<IEvsUltrasonicsArray>& evsUltrasonicsArray) override; 
    Return<void> getUltrasonicsArrayList(getUltrasonicsArrayList_cb _hidl_cb) override;

    // Dump apis
    Return<void> debug(const hidl_handle& fd, const hidl_vec<hidl_string>& args) override;
    void cmdDump(int fd, const hidl_vec<hidl_string>& options);
    void cmdHelp(int fd);
    void cmdList(int fd, const hidl_vec<hidl_string>& options);
    void cmdDumpDevice(int fd, const hidl_vec<hidl_string>& options);
    bool validStreamCfg(const Stream& streamCfg);

    // Implementation details
    EvsEnumerator(sp<IAutomotiveDisplayProxyService> proxyService = nullptr);

private:
    struct CameraRecord {
        std::string         name;
        CameraDesc          desc;
        wp<EvsCamera>    activeInstance;

        CameraRecord(const char *name, const char *cameraId)
            : desc() { this->name = name; desc.v1.cameraId = cameraId; }
    };


    static bool EnumAvailableVideo();
    static bool qualifyCaptureDevice(const char* deviceName);
    static bool filterVideoFromConfigure(char *deviceName);
    static CameraRecord* findCameraById(const std::string& cameraId);

    static std::unique_ptr<ConfigManager>   sConfigManager;

    class PollVideoFileThread : public Thread {
    public:
        PollVideoFileThread();
    private:
        virtual void onFirstRef();
        virtual int32_t readyToRun();
        virtual bool threadLoop();
        int mINotifyFd;
        int mINotifyWd;
        int mEpollFd;
    };

    sp<PollVideoFileThread> mPollVideoFileThread;

    // NOTE:  All members values are static so that all clients operate on the same state
    //        That is to say, this is effectively a singleton despite the fact that HIDL
    //        constructs a new instance for each client.
    //        Because our server has a single thread in the thread pool, these values are
    //        never accessed concurrently despite potentially having multiple instance objects
    //        using them.
    static std::list<CameraRecord> sCameraList;
    static std::mutex                       sLock;

    static wp<EvsDisplay>          sActiveDisplay; // Weak pointer. Object destructs if client dies.
};

} // namespace implementation
} // namespace V1_1
} // namespace evs
} // namespace automotive
} // namespace hardware
} // namespace android

#endif  // ANDROID_HARDWARE_AUTOMOTIVE_EVS_V1_0_EVSCAMERAENUMERATOR_H
