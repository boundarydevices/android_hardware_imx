/*
 * Copyright (C) 2020 The Android Open Source Project
 * Copyright 2021 NXP
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

#pragma once
#include "Sensor.h"
#include <V2_1/SubHal.h>

namespace nxp_sensors_subhal {

using ::android::hardware::sensors::V2_1::implementation::IHalProxyCallback;

using ::android::hardware::sensors::V1_0::RateLevel;
using ::android::hardware::sensors::V1_0::SharedMemInfo;

using ::android::hardware::hidl_handle;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::sp;

/**
 * Implementation of a ISensorsSubHal that can be used as a reference HAL implementation of sensors
 * multihal 2.0.
 */
class SensorsSubHal : public ::android::hardware::sensors::V2_1::implementation::ISensorsSubHal, public ISensorsEventCallback {

  public:
    SensorsSubHal();
    ~SensorsSubHal();

    // Methods from ::android::hardware::sensors::V2_1::ISensors follow.
    Return<void> getSensorsList_2_1(getSensorsList_2_1_cb _hidl_cb) override;

    Return<Result> setOperationMode(OperationMode mode) override;

    OperationMode getOperationMode() const { return mCurrentOperationMode; }

    Return<Result> activate(int32_t sensorHandle, bool enabled) override;

    Return<Result> batch(int32_t sensorHandle, int64_t samplingPeriodNs,
                         int64_t maxReportLatencyNs) override;

    Return<Result> flush(int32_t sensorHandle) override;

    Return<Result> injectSensorData_2_1(const Event& event) override;

    Return<void> registerDirectChannel(const SharedMemInfo& mem,
                                       registerDirectChannel_cb _hidl_cb) override;

    Return<Result> unregisterDirectChannel(int32_t channelHandle) override;

    Return<void> configDirectReport(int32_t sensorHandle, int32_t channelHandle, RateLevel rate,
                                    configDirectReport_cb _hidl_cb) override;

    Return<void> debug(const hidl_handle& fd, const hidl_vec<hidl_string>& args) override;

    // Methods from ::android::hardware::sensors::V2_0::implementation::ISensorsSubHal follow.
    const std::string getName() override { return "nxp-IIO-SensorsSubhal"; }

    Return<Result> initialize(const sp<IHalProxyCallback>& halProxyCallback) override;

    // Method from ISensorsEventCallback.
    void postEvents(const std::vector<Event>& events, bool wakeup) override;

  protected:
    void AddSensor(struct iio_device_data& iio_data);

    /**
     * A map of the available sensors
     */
    std::map<int32_t, std::unique_ptr<SensorBase>> mSensors;

    /**
     * Callback used to communicate to the HalProxy when dynamic sensors are connected /
     * disconnected, sensor events need to be sent to the framework, and when a wakelock should be
     * acquired.
     */
    sp<IHalProxyCallback> mCallback;

  private:
    /**
     * The current operation mode of the multihal framework. Ensures that all subhals are set to
     * the same operation mode.
     */
    OperationMode mCurrentOperationMode = OperationMode::NORMAL;

    /**
     * The next available sensor handle
     */
    int32_t mNextHandle;
};

}  // namespace nxp_sensors_subhal
