/*
 * Copyright (C) 2016 The Android Open Source Project
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
#include "EvsStateControl.h"

#include <binder/IServiceManager.h>
#include <inttypes.h>
#include <log/log.h>
#include <stdio.h>
#include <string.h>
#include <system/camera_metadata.h>
#include <utils/SystemClock.h>

#include "FormatConvert.h"
#include "RenderDirectView.h"
#include "RenderPixelCopy.h"
#include "RenderTopView.h"

using BufferDesc_1_0 = ::android::hardware::automotive::evs::V1_0::BufferDesc;
using ::android::hardware::automotive::evs::V1_0::EvsResult;
using ::android::hardware::graphics::common::V1_0::PixelFormat;
const size_t kStreamCfgSz = sizeof(RawStreamConfig);

static bool isSfReady() {
    const android::String16 serviceName("SurfaceFlinger");
    return android::defaultServiceManager()->checkService(serviceName) != nullptr;
}

// TODO:  Seems like it'd be nice if the Vehicle HAL provided such helpers (but how & where?)
inline constexpr VehiclePropertyType getPropType(VehicleProperty prop) {
    return static_cast<VehiclePropertyType>(static_cast<int32_t>(prop) &
                                            static_cast<int32_t>(VehiclePropertyType::MASK));
}

EvsStateControl::EvsStateControl(android::sp<IVehicle> pVnet, android::sp<IEvsEnumerator> pEvs,
                                 android::sp<IEvsDisplay> pDisplay, const ConfigManager& config)
      : mVehicle(pVnet), mEvs(pEvs), mDisplay(pDisplay), mConfig(config), mCurrentState(OFF) {
    // Initialize the property value containers we'll be updating (they'll be zeroed by default)
    static_assert(getPropType(VehicleProperty::GEAR_SELECTION) == VehiclePropertyType::INT32,
                  "Unexpected type for GEAR_SELECTION property");
    static_assert(getPropType(VehicleProperty::TURN_SIGNAL_STATE) == VehiclePropertyType::INT32,
                  "Unexpected type for TURN_SIGNAL_STATE property");

    mGearValue.prop = static_cast<int32_t>(VehicleProperty::GEAR_SELECTION);
    mTurnSignalValue.prop = static_cast<int32_t>(VehicleProperty::TURN_SIGNAL_STATE);

#if 1
    // This way we only ever deal with cameras which exist in the system
    // Build our set of cameras for the states we support
    ALOGD("Requesting camera list");
    mEvs->getCameraList_1_1([this, &config](hidl_vec<CameraDesc> cameraList) {
        ALOGI("Camera list callback received %zu cameras", cameraList.size());
        for (auto&& cam : cameraList) {
            ALOGD("Found camera %s", cam.v1.cameraId.c_str());
            bool cameraConfigFound = false;

            // Check our configuration for information about this camera
            // Note that a camera can have a compound function string
            // such that a camera can be "right/reverse" and be used for both.
            // If more than one camera is listed for a given function, we'll
            // list all of them and let the UX/rendering logic use one, some
            // or all of them as appropriate.
            for (auto&& info : config.getCameras()) {
                if (strstr(cam.v1.cameraId.c_str(), info.cameraId.c_str())) {
                    // We found a match!
                    if (info.function.find("reverse") != std::string::npos) {
                        mCameraList[State::REVERSE].push_back(info);
                        mCameraDescList[State::REVERSE].emplace_back(cam);
                    }
                    if (info.function.find("right") != std::string::npos) {
                        mCameraList[State::RIGHT].push_back(info);
                        mCameraDescList[State::RIGHT].emplace_back(cam);
                    }
                    if (info.function.find("left") != std::string::npos) {
                        mCameraList[State::LEFT].push_back(info);
                        mCameraDescList[State::LEFT].emplace_back(cam);
                    }
                    if (info.function.find("park") != std::string::npos) {
                        mCameraList[State::PARKING].push_back(info);
                        mCameraDescList[State::PARKING].emplace_back(cam);
                    }
                    cameraConfigFound = true;
                    break;
                }
            }
            if (!cameraConfigFound) {
                ALOGW("No config information for hardware camera %s", cam.v1.cameraId.c_str());
            }
        }
    });
#else // This way we use placeholders for cameras in the configuration but not reported by EVS
    // Build our set of cameras for the states we support
    ALOGD("Requesting camera list");
    for (auto&& info : config.getCameras()) {
        if (info.function.find("reverse") != std::string::npos) {
            mCameraList[State::REVERSE].push_back(info);
        }
        if (info.function.find("right") != std::string::npos) {
            mCameraList[State::RIGHT].push_back(info);
        }
        if (info.function.find("left") != std::string::npos) {
            mCameraList[State::LEFT].push_back(info);
        }
        if (info.function.find("park") != std::string::npos) {
            mCameraList[State::PARKING].push_back(info);
        }
    }
#endif

    ALOGD("State controller ready");
}

bool EvsStateControl::startUpdateLoop() {
    // Create the thread and report success if it gets started
    mRenderThread = std::thread([this]() { updateLoop(); });
    return mRenderThread.joinable();
}

void EvsStateControl::postCommand(const Command& cmd) {
    // Push the command onto the queue watched by updateLoop
    mLock.lock();
    mCommandQueue.push(cmd);
    mLock.unlock();

    // Send a signal to wake updateLoop in case it is asleep
    mWakeSignal.notify_all();
}

void EvsStateControl::updateLoop() {
    ALOGD("Starting EvsStateControl update loop");

    bool run = true;
    while (run) {
        // Process incoming commands
        {
            std::lock_guard<std::mutex> lock(mLock);
            while (!mCommandQueue.empty()) {
                const Command& cmd = mCommandQueue.front();
                switch (cmd.operation) {
                    case Op::EXIT:
                        run = false;
                        break;
                    case Op::CHECK_VEHICLE_STATE:
                        // Just running selectStateForCurrentConditions below will take care of this
                        break;
                    case Op::TOUCH_EVENT:
                        // Implement this given the x/y location of the touch event
                        break;
                }
                mCommandQueue.pop();
            }
        }

        // Review vehicle state and choose an appropriate renderer
        if (!selectStateForCurrentConditions()) {
            ALOGE("selectStateForCurrentConditions failed so we're going to die");
            break;
        }

        // If we have an active renderer, give it a chance to draw
        if (mCurrentRenderer) {
            // Get the output buffer we'll use to display the imagery
            BufferDesc_1_0 tgtBuffer = {};
            mDisplay->getTargetBuffer(
                    [&tgtBuffer](const BufferDesc_1_0& buff) { tgtBuffer = buff; });

            if (tgtBuffer.memHandle == nullptr) {
                ALOGE("Didn't get requested output buffer -- skipping this frame.");
            } else {
                // Generate our output image
                if (!mCurrentRenderer->drawFrame(convertBufferDesc(tgtBuffer))) {
                    // If drawing failed, we want to exit quickly so an app restart can happen
                    run = false;
                }

                // Send the finished image back for display
                mDisplay->returnTargetBufferForDisplay(tgtBuffer);
            }
        } else {
            // No active renderer, so sleep until somebody wakes us with another command
            std::unique_lock<std::mutex> lock(mLock);
            mWakeSignal.wait(lock);
        }
    }

    ALOGW("EvsStateControl update loop ending");

    // TODO:  Fix it so we can exit cleanly from the main thread instead
    printf("Shutting down app due to state control loop ending\n");
    ALOGE("KILLING THE APP FROM THE EvsStateControl LOOP ON DRAW FAILURE!!!");
    exit(1);
}

bool EvsStateControl::selectStateForCurrentConditions() {
    static int32_t sDummyGear = int32_t(VehicleGear::GEAR_REVERSE);
    static int32_t sDummySignal = int32_t(VehicleTurnSignal::NONE);

    if (mVehicle != nullptr) {
        // Query the car state
        if (invokeGet(&mGearValue) != StatusCode::OK) {
            ALOGE("GEAR_SELECTION not available from vehicle.  Exiting.");
            return false;
        }
        if ((mTurnSignalValue.prop == 0) || (invokeGet(&mTurnSignalValue) != StatusCode::OK)) {
            // Silently treat missing turn signal state as no turn signal active
            mTurnSignalValue.value.int32Values.setToExternal(&sDummySignal, 1);
            mTurnSignalValue.prop = 0;
        }
    } else {
        // While testing without a vehicle, behave as if we're in reverse for the first 20 seconds
        static const int kShowTime = 20; // seconds

        // See if it's time to turn off the default reverse camera
        static std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start).count() > kShowTime) {
            // Switch to drive (which should turn off the reverse camera)
            sDummyGear = int32_t(VehicleGear::GEAR_DRIVE);
        }

        // Build the dummy vehicle state values (treating single values as 1 element vectors)
        mGearValue.value.int32Values.setToExternal(&sDummyGear, 1);
        mTurnSignalValue.value.int32Values.setToExternal(&sDummySignal, 1);
    }

    // Choose our desired EVS state based on the current car state
    // TODO:  Update this logic, and consider user input when choosing if a view should be presented
    State desiredState = OFF;
    if (mGearValue.value.int32Values[0] == int32_t(VehicleGear::GEAR_REVERSE)) {
        desiredState = REVERSE;
    } else if (mTurnSignalValue.value.int32Values[0] == int32_t(VehicleTurnSignal::RIGHT)) {
        desiredState = RIGHT;
    } else if (mTurnSignalValue.value.int32Values[0] == int32_t(VehicleTurnSignal::LEFT)) {
        desiredState = LEFT;
    } else if (mGearValue.value.int32Values[0] == int32_t(VehicleGear::GEAR_PARK)) {
        desiredState = PARKING;
    }

    // Apply the desire state
    return configureEvsPipeline(desiredState);
}

StatusCode EvsStateControl::invokeGet(VehiclePropValue* pRequestedPropValue) {
    StatusCode status = StatusCode::TRY_AGAIN;

    // Call the Vehicle HAL, which will block until the callback is complete
    mVehicle->get(*pRequestedPropValue,
                  [pRequestedPropValue, &status](StatusCode s, const VehiclePropValue& v) {
                      status = s;
                      if (s == StatusCode::OK) {
                          *pRequestedPropValue = v;
                      }
                  });

    return status;
}

bool EvsStateControl::configureEvsPipeline(State desiredState) {
    static bool isGlReady = false;

    if (mCurrentState == desiredState) {
        // Nothing to do here...
        return true;
    }

    ALOGD("Switching to state %d.", desiredState);
    ALOGD("  Current state %d has %zu cameras", mCurrentState, mCameraList[mCurrentState].size());
    ALOGD("  Desired state %d has %zu cameras", desiredState, mCameraList[desiredState].size());

    if (!isGlReady && !isSfReady()) {
        // Graphics is not ready yet; using CPU renderer.
        if (mCameraList[desiredState].size() >= 1) {
            mDesiredRenderer =
                    std::make_unique<RenderPixelCopy>(mEvs, mCameraList[desiredState][0]);
            if (!mDesiredRenderer) {
                ALOGE("Failed to construct Pixel Copy renderer.  Skipping state change.");
                return false;
            }
        } else {
            ALOGD("Unsupported, desiredState %d has %u cameras.", desiredState,
                  static_cast<unsigned int>(mCameraList[desiredState].size()));
        }
    } else {
        // Assumes that SurfaceFlinger is available always after being launched.

        // Do we need a new direct view renderer?
        if (mCameraList[desiredState].size() == 1) {
            // We have a camera assigned to this state for direct view.
            bool foundCfg = false;
            std::unique_ptr<Stream> targetCfg(new Stream());
            if (!foundCfg) {
                // This logic picks the first configuration in the list among them that
                // support YUYV format and its frame rate is faster than minReqFps.
                const int32_t minReqFps = 15;
                int32_t maxArea = 0;
                camera_metadata_entry_t streamCfgs;
                if (!find_camera_metadata_entry(reinterpret_cast<camera_metadata_t*>(
                                                        mCameraDescList[desiredState][0]
                                                                .metadata.data()),
                                                ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                                                &streamCfgs)) {
                    // Stream configurations are found in metadata
                    // the size of every stream is kStreamCfgSz
                    RawStreamConfig* ptr = reinterpret_cast<RawStreamConfig*>(streamCfgs.data.i32);
                    unsigned streamCfgSize = calculate_camera_metadata_entry_data_size(
                            get_camera_metadata_tag_type(
                                    ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS),
                            streamCfgs.count);

                    for (unsigned idx = 0; idx < streamCfgSize; idx += kStreamCfgSz) {
                        if (ptr->direction ==
                                    ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT &&
                            ptr->format == HAL_PIXEL_FORMAT_RGB_888) {
                            if (ptr->framerate >= minReqFps && ptr->width * ptr->height > maxArea) {
                                targetCfg->id = ptr->id;
                                targetCfg->width = ptr->width;
                                targetCfg->height = ptr->height;
                                maxArea = ptr->width * ptr->height;
                                foundCfg = true;
                            }
                        }
                        ++ptr;
                    }
                } else {
                    ALOGE("No stream configuration data is found; ");
                }
            }

            targetCfg->format = static_cast<PixelFormat>(HAL_PIXEL_FORMAT_RGB_888);

            mDesiredRenderer =
                    std::make_unique<RenderDirectView>(mEvs, mCameraDescList[desiredState][0],
                                                       std::move(targetCfg));
            if (!mDesiredRenderer) {
                ALOGE("Failed to construct direct renderer.  Skipping state change.");
                return false;
            }
        } else if (mCameraList[desiredState].size() > 1 || desiredState == PARKING) {
            std::unique_ptr<Stream> targetCfg(new Stream());

            targetCfg->width = WIDTH_FOR_TOP_VIEW;
            targetCfg->height = HEIGHT_FOR_TOP_VIEW;
            targetCfg->format = static_cast<PixelFormat>(HAL_PIXEL_FORMAT_RGB_888);

            mDesiredRenderer = std::make_unique<RenderTopView>(mEvs, mCameraList[desiredState],
                                                               mConfig, std::move(targetCfg));
            if (!mDesiredRenderer) {
                ALOGE("Failed to construct top view renderer.  Skipping state change.");
                return false;
            }
        } else {
            ALOGD("Unsupported, desiredState %d has %u cameras.", desiredState,
                  static_cast<unsigned int>(mCameraList[desiredState].size()));
        }

        // GL renderer is now ready.
        isGlReady = true;
    }

    // Since we're changing states, shut down the current renderer
    if (mCurrentRenderer != nullptr) {
        mCurrentRenderer->deactivate();
        mCurrentRenderer = nullptr; // It's a smart pointer, so destructs on assignment to null
    }

    // Now set the display state based on whether we have a video feed to show
    if (mDesiredRenderer == nullptr) {
        ALOGD("Turning off the display");
        mDisplay->setDisplayState(EvsDisplayState::NOT_VISIBLE);
    } else {
        mCurrentRenderer = std::move(mDesiredRenderer);

        // Start the camera stream
        ALOGD("EvsStartCameraStreamTiming start time: %" PRId64 "ms", android::elapsedRealtime());
        if (!mCurrentRenderer->activate()) {
            ALOGE("New renderer failed to activate");
            return false;
        }

        // Activate the display
        ALOGD("EvsActivateDisplayTiming start time: %" PRId64 "ms", android::elapsedRealtime());
        Return<EvsResult> result =
                mDisplay->setDisplayState(EvsDisplayState::VISIBLE_ON_NEXT_FRAME);
        if (result != EvsResult::OK) {
            ALOGE("setDisplayState returned an error (%d)", (EvsResult)result);
            return false;
        }
    }

    // Record our current state
    ALOGI("Activated state %d.", desiredState);
    mCurrentState = desiredState;

    return true;
}
