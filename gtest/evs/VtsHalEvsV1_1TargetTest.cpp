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

#define LOG_TAG "VtsHalEvsTest"

// These values are called out in the EVS design doc (as of Mar 8, 2017)
static const int kMaxStreamStartMilliseconds = 500;
static const int kMinimumFramesPerSecond = 10;

// static const int kSecondsToMilliseconds = 1000;
// static const int kMillisecondsToMicroseconds = 1000;
static const float kNanoToMilliseconds = 0.000001f;
static const float kNanoToSeconds = 0.000000001f;
static const int kMillisecondsToMicroseconds = 1000;

#include <android-base/logging.h>
#include <android/hardware/automotive/evs/1.1/IEvsCamera.h>
#include <android/hardware/automotive/evs/1.1/IEvsCameraStream.h>
#include <android/hardware/automotive/evs/1.1/IEvsDisplay.h>
#include <android/hardware/automotive/evs/1.1/IEvsEnumerator.h>
#include <android/hardware/camera/device/3.2/ICameraDevice.h>
#include <gtest/gtest.h>
#include <hidl/GtestPrinter.h>
#include <hidl/HidlTransportSupport.h>
#include <hidl/ServiceManagement.h>
#include <hwbinder/ProcessState.h>
#include <system/camera_metadata.h>
#include <ui/DisplayMode.h>
#include <ui/DisplayState.h>
#include <ui/GraphicBuffer.h>
#include <ui/GraphicBufferAllocator.h>
#include <utils/Errors.h>
#include <utils/StrongPointer.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <unordered_set>

#include "FrameHandler.h"
#include "FrameHandlerUltrasonics.h"

using namespace ::android::hardware::automotive::evs::V1_1;
using namespace std::chrono_literals;

using ::android::sp;
using ::android::wp;
using ::android::hardware::hidl_handle;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::automotive::evs::V1_0::DisplayDesc;
using ::android::hardware::automotive::evs::V1_0::DisplayState;
using ::android::hardware::automotive::evs::V1_1::BufferDesc;
using ::android::hardware::camera::device::V3_2::Stream;
using ::android::hardware::graphics::common::V1_0::PixelFormat;
using IEvsCamera_1_0 = ::android::hardware::automotive::evs::V1_0::IEvsCamera;
using IEvsCamera_1_1 = ::android::hardware::automotive::evs::V1_1::IEvsCamera;
using IEvsDisplay_1_0 = ::android::hardware::automotive::evs::V1_0::IEvsDisplay;
using IEvsDisplay_1_1 = ::android::hardware::automotive::evs::V1_1::IEvsDisplay;

/*
 * Plese note that this is different from what is defined in
 * libhardware/modules/camera/3_4/metadata/types.h; this has one additional
 * field to store a framerate.
 */
// const size_t kStreamCfgSz = 5;
typedef struct {
    int32_t width;
    int32_t height;
    int32_t format;
    int32_t direction;
    int32_t framerate;
} RawStreamConfig;

// The main test class for EVS
class EvsHidlTest : public ::testing::TestWithParam<std::string> {
public:
    virtual void SetUp() override {
        // Make sure we can connect to the enumerator
        std::string service_name = GetParam();
        pEnumerator = IEvsEnumerator::getService(service_name);
        ASSERT_NE(pEnumerator.get(), nullptr);
        LOG(INFO) << "Test target service: " << service_name;

        mIsHwModule = pEnumerator->isHardware();
    }

    virtual void TearDown() override {
        // Attempt to close any active camera
        for (auto&& cam : activeCameras) {
            if (cam != nullptr) {
                pEnumerator->closeCamera(cam);
            }
        }
        activeCameras.clear();
    }

protected:
    void loadCameraList() {
        // SetUp() must run first!
        assert(pEnumerator != nullptr);

        // Get the camera list
        pEnumerator->getCameraList_1_1([this](hidl_vec<CameraDesc> cameraList) {
            LOG(INFO) << "Camera list callback received " << cameraList.size() << " cameras";
            cameraInfo.reserve(cameraList.size());
            for (auto&& cam : cameraList) {
                LOG(INFO) << "Found camera " << cam.v1.cameraId;
                cameraInfo.push_back(cam);
            }
        });
    }

    void loadUltrasonicsArrayList() {
        // SetUp() must run first!
        assert(pEnumerator != nullptr);

        // Get the ultrasonics array list
        pEnumerator->getUltrasonicsArrayList([this](hidl_vec<UltrasonicsArrayDesc> ultraList) {
            LOG(INFO) << "Ultrasonics array list callback received " << ultraList.size()
                      << " arrays";
            ultrasonicsArraysInfo.reserve(ultraList.size());
            for (auto&& ultraArray : ultraList) {
                LOG(INFO) << "Found ultrasonics array " << ultraArray.ultrasonicsArrayId;
                ultrasonicsArraysInfo.push_back(ultraArray);
            }
        });
    }

    bool isLogicalCamera(const camera_metadata_t* metadata) {
        if (metadata == nullptr) {
            // A logical camera device must have a valid camera metadata.
            return false;
        }

        // Looking for LOGICAL_MULTI_CAMERA capability from metadata.
        camera_metadata_ro_entry_t entry;
        int rc = find_camera_metadata_ro_entry(metadata, ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                                               &entry);
        if (0 != rc) {
            // No capabilities are found.
            return false;
        }

        for (size_t i = 0; i < entry.count; ++i) {
            uint8_t cap = entry.data.u8[i];
            if (cap == ANDROID_REQUEST_AVAILABLE_CAPABILITIES_LOGICAL_MULTI_CAMERA) {
                return true;
            }
        }

        return false;
    }

    std::unordered_set<std::string> getPhysicalCameraIds(const std::string& id, bool& flag) {
        std::unordered_set<std::string> physicalCameras;

        auto it = cameraInfo.begin();
        while (it != cameraInfo.end()) {
            if (it->v1.cameraId == id) {
                break;
            }
            ++it;
        }

        if (it == cameraInfo.end()) {
            // Unknown camera is requested.  Return an empty list.
            return physicalCameras;
        }

        const camera_metadata_t* metadata = reinterpret_cast<camera_metadata_t*>(&it->metadata[0]);
        flag = isLogicalCamera(metadata);
        if (!flag) {
            // EVS assumes that the device w/o a valid metadata is a physical
            // device.
            LOG(INFO) << id << " is not a logical camera device.";
            physicalCameras.emplace(id);
            return physicalCameras;
        }

        // Look for physical camera identifiers
        camera_metadata_ro_entry entry;
        int rc = find_camera_metadata_ro_entry(metadata, ANDROID_LOGICAL_MULTI_CAMERA_PHYSICAL_IDS,
                                               &entry);
        if (rc != 0) {
            LOG(ERROR) << "No physical camera ID is found for a logical camera device";
        }

        const uint8_t* ids = entry.data.u8;
        size_t start = 0;
        for (size_t i = 0; i < entry.count; ++i) {
            if (ids[i] == '\0') {
                if (start != i) {
                    std::string id(reinterpret_cast<const char*>(ids + start));
                    physicalCameras.emplace(id);
                }
                start = i + 1;
            }
        }

        LOG(INFO) << id << " consists of " << physicalCameras.size() << " physical camera devices";
        return physicalCameras;
    }

    sp<IEvsEnumerator> pEnumerator;               // Every test needs access to the service
    std::vector<CameraDesc> cameraInfo;           // Empty unless/until loadCameraList() is called
    bool mIsHwModule;                             // boolean to tell current module under testing
                                                  // is HW module implementation.
    std::deque<sp<IEvsCamera_1_1>> activeCameras; // A list of active camera handles that are
                                                  // needed to be cleaned up.
    std::vector<UltrasonicsArrayDesc> ultrasonicsArraysInfo; // Empty unless/until
                                                             // loadUltrasonicsArrayList() is called
    std::deque<wp<IEvsCamera_1_1>> activeUltrasonicsArrays;  // A list of active ultrasonic array
                                                             // handles that are to be cleaned up.
};

// Test cases, their implementations, and corresponding requirements are
// documented at go/aae-evs-public-api-test.

/*
 * CameraOpenClean:
 * Opens each camera reported by the enumerator and then explicitly closes it via a
 * call to closeCamera.  Then repeats the test to ensure all cameras can be reopened.
 */
TEST_P(EvsHidlTest, LogicCameraStreamPerformance) {
    LOG(INFO) << "Starting LogicCameraStreamPerformance test";

    // Get the camera list
    loadCameraList();

    // Using null stream configuration makes EVS uses the default resolution and
    // output format.
    Stream nullCfg = {};

    // Test each reported camera
    for (auto&& cam : cameraInfo) {
        bool isLogicalCam = false;
        auto devices = getPhysicalCameraIds(cam.v1.cameraId, isLogicalCam);
        if (mIsHwModule && !isLogicalCam) {
            LOG(INFO) << "Skip a pysical device " << cam.v1.cameraId;
            continue;
        }

        sp<IEvsCamera_1_1> pCam =
                IEvsCamera_1_1::castFrom(pEnumerator->openCamera_1_1(cam.v1.cameraId, nullCfg))
                        .withDefault(nullptr);
        ASSERT_NE(pCam, nullptr);

        // Store a camera handle for a clean-up
        activeCameras.push_back(pCam);

        // Set up a frame receiver object which will fire up its own thread
        sp<FrameHandler> frameHandler =
                new FrameHandler(pCam, cam, nullptr, FrameHandler::eAutoReturn);

        // Start the camera's video stream
        nsecs_t start = systemTime(SYSTEM_TIME_MONOTONIC);

        bool startResult = frameHandler->startStream();
        ASSERT_TRUE(startResult);

        // Ensure the first frame arrived within the expected time
        frameHandler->waitForFrameCount(1);
        nsecs_t firstFrame = systemTime(SYSTEM_TIME_MONOTONIC);
        nsecs_t timeToFirstFrame = systemTime(SYSTEM_TIME_MONOTONIC) - start;

        // Extra delays are expected when we attempt to start a video stream on
        // the logical camera device.  The amount of delay is expected the
        // number of physical camera devices multiplied by
        // kMaxStreamStartMilliseconds at most.
        EXPECT_LE(nanoseconds_to_milliseconds(timeToFirstFrame),
                  kMaxStreamStartMilliseconds * devices.size());
        printf("%s: Measured time to first frame %0.2f ms\n", cam.v1.cameraId.c_str(),
               timeToFirstFrame * kNanoToMilliseconds);
        LOG(INFO) << cam.v1.cameraId << ": Measured time to first frame " << std::scientific
                  << timeToFirstFrame * kNanoToMilliseconds << " ms.";

        // Check aspect ratio
        unsigned width = 0, height = 0;
        frameHandler->getFrameDimension(&width, &height);
        EXPECT_GE(width, height);

        // Wait a bit, then ensure we get at least the required minimum number of frames
        sleep(5);
        nsecs_t end = systemTime(SYSTEM_TIME_MONOTONIC);

        // Even when the camera pointer goes out of scope, the FrameHandler object will
        // keep the stream alive unless we tell it to shutdown.
        // Also note that the FrameHandle and the Camera have a mutual circular reference, so
        // we have to break that cycle in order for either of them to get cleaned up.
        frameHandler->shutdown();

        unsigned framesReceived = 0;
        frameHandler->getFramesCounters(&framesReceived, nullptr);
        framesReceived = framesReceived - 1; // Back out the first frame we already waited for
        nsecs_t runTime = end - firstFrame;
        float framesPerSecond = framesReceived / (runTime * kNanoToSeconds);
        printf("Measured camera rate %3.2f fps\n", framesPerSecond);
        LOG(INFO) << "Measured camera rate " << std::scientific << framesPerSecond << " fps.";
        EXPECT_GE(framesPerSecond, kMinimumFramesPerSecond);

        // Explicitly release the camera
        pEnumerator->closeCamera(pCam);
        activeCameras.clear();
    }
}

// Sets frames in flight before and after start of stream and verfies success.
TEST_P(EvsHidlTest, UltrasonicsSetFramesInFlight) {
    LOG(INFO) << "Starting UltrasonicsSetFramesInFlight";

    // Get the ultrasonics array list
    loadUltrasonicsArrayList();

    // For each ultrasonics array.
    for (auto&& ultraInfo : ultrasonicsArraysInfo) {
        LOG(DEBUG) << "Testing ultrasonics array: " << ultraInfo.ultrasonicsArrayId;

        sp<IEvsUltrasonicsArray> pUltrasonicsArray =
                pEnumerator->openUltrasonicsArray(ultraInfo.ultrasonicsArrayId);
        ASSERT_NE(pUltrasonicsArray, nullptr);

        EvsResult result = pUltrasonicsArray->setMaxFramesInFlight(10);
        EXPECT_EQ(result, EvsResult::OK);

        sp<FrameHandlerUltrasonics> frameHandler = new FrameHandlerUltrasonics(pUltrasonicsArray);

        // Start stream.
        result = pUltrasonicsArray->startStream(frameHandler);
        ASSERT_EQ(result, EvsResult::OK);

        result = pUltrasonicsArray->setMaxFramesInFlight(5);
        EXPECT_EQ(result, EvsResult::OK);

        // Stop stream.
        pUltrasonicsArray->stopStream();

        // Explicitly close the ultrasonics array so resources are released right away
        pEnumerator->closeUltrasonicsArray(pUltrasonicsArray);
    }
}

/*
 * CameraStreamBuffering:
 * Ensure the camera implementation behaves properly when the client holds onto buffers for more
 * than one frame time.  The camera must cleanly skip frames until the client is ready again.
 */
TEST_P(EvsHidlTest, CameraStreamBuffering) {
    LOG(INFO) << "Starting CameraStreamBuffering test";

    // Arbitrary constant (should be > 1 and less than crazy)
    static const unsigned int kBuffersToHold = 6;

    // Get the camera list
    loadCameraList();

    // Using null stream configuration makes EVS uses the default resolution and
    // output format.
    Stream nullCfg = {};

    // Test each reported camera
    for (auto&& cam : cameraInfo) {
        bool isLogicalCam = false;
        getPhysicalCameraIds(cam.v1.cameraId, isLogicalCam);
        if (mIsHwModule && isLogicalCam) {
            LOG(INFO) << "Skip a logical device " << cam.v1.cameraId << " for HW target.";
            continue;
        }

        sp<IEvsCamera_1_1> pCam =
                IEvsCamera_1_1::castFrom(pEnumerator->openCamera_1_1(cam.v1.cameraId, nullCfg))
                        .withDefault(nullptr);
        ASSERT_NE(pCam, nullptr);

        // Store a camera handle for a clean-up
        activeCameras.push_back(pCam);

        // Ask for a crazy number of buffers in flight to ensure it errors correctly
        Return<EvsResult> badResult = pCam->setMaxFramesInFlight(0xFFFFFFFF);
        EXPECT_EQ(EvsResult::BUFFER_NOT_AVAILABLE, badResult);

        // Now ask for exactly two buffers in flight as we'll test behavior in that case
        Return<EvsResult> goodResult = pCam->setMaxFramesInFlight(kBuffersToHold);
        EXPECT_EQ(EvsResult::OK, goodResult);

        // Set up a frame receiver object which will fire up its own thread.
        sp<FrameHandler> frameHandler =
                new FrameHandler(pCam, cam, nullptr, FrameHandler::eNoAutoReturn);

        // Start the camera's video stream
        bool startResult = frameHandler->startStream();
        ASSERT_TRUE(startResult);

        // Check that the video stream stalls once we've gotten exactly the number of buffers
        // we requested since we told the frameHandler not to return them.
        sleep(1); // 1 second should be enough for at least 5 frames to be delivered worst case
        unsigned framesReceived = 0;
        frameHandler->getFramesCounters(&framesReceived, nullptr);
        ASSERT_EQ(kBuffersToHold, framesReceived) << "Stream didn't stall at expected buffer limit";

        // Give back one buffer
        bool didReturnBuffer = frameHandler->returnHeldBuffer();
        EXPECT_TRUE(didReturnBuffer);

        // Once we return a buffer, it shouldn't take more than 1/10 second to get a new one
        // filled since we require 10fps minimum -- but give a 10% allowance just in case.
        usleep(220 * kMillisecondsToMicroseconds);
        frameHandler->getFramesCounters(&framesReceived, nullptr);
        EXPECT_EQ(kBuffersToHold + 1, framesReceived) << "Stream should've resumed";

        // Even when the camera pointer goes out of scope, the FrameHandler object will
        // keep the stream alive unless we tell it to shutdown.
        // Also note that the FrameHandle and the Camera have a mutual circular reference, so
        // we have to break that cycle in order for either of them to get cleaned up.
        frameHandler->shutdown();

        // Explicitly release the camera
        pEnumerator->closeCamera(pCam);
        activeCameras.clear();
    }
}

INSTANTIATE_TEST_SUITE_P(
        PerInstance, EvsHidlTest,
        testing::ValuesIn(android::hardware::getAllHalInstanceNames(IEvsEnumerator::descriptor)),
        android::hardware::PrintInstanceNameToString);
