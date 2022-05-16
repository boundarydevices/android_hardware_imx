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
#include <android/hardware/sensors/2.1/types.h>
#include <poll.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <android-base/properties.h>
#include <hardware/sensors.h>
#include <log/log.h>
#include <utils/SystemClock.h>
#include <cmath>
#include <sys/socket.h>
#include <inttypes.h>
#include "iio_utils.h"

#define NUM_OF_CHANNEL_SUPPORTED 4
// Subtract the timestamp channel to get the number of data channels
#define NUM_OF_DATA_CHANNELS NUM_OF_CHANNEL_SUPPORTED - 1

using ::android::hardware::sensors::V1_0::AdditionalInfo;
using ::android::hardware::sensors::V1_0::OperationMode;
using ::android::hardware::sensors::V1_0::Result;
using ::android::hardware::sensors::V1_0::SensorFlagBits;
using ::android::hardware::sensors::V1_0::MetaDataEventType;
using ::android::hardware::sensors::V2_1::SensorInfo;
using ::android::hardware::sensors::V2_1::SensorType;
using ::android::hardware::sensors::V2_1::Event;
using ::android::hardware::sensors::V1_0::SensorStatus;

using ::android::hardware::Return;
using ::android::status_t;
using ::android::base::GetProperty;

namespace nxp_sensors_subhal {

static constexpr unsigned int frequency_to_us(unsigned int x) {
    return (1E6 / x);
}

static constexpr unsigned int ns_to_frequency(unsigned int x) {
    return (1E9 / x);
}

// SCMI IIO driver gives sensor power in microwatts. Sensor HAL expects
// it in mA. Conversion process needs the input voltage for the IMU.
// Based on commonly used IMUs, 3.6V is picked as the default.
constexpr auto SENSOR_VOLTAGE_DEFAULT = 3.6f;

static constexpr char kTriggerType[] = "vendor.sensor.trigger";

class ISensorsEventCallback {
  public:
    virtual ~ISensorsEventCallback() = default;
    virtual void postEvents(const std::vector<Event>& events, bool wakeup) = 0;
};

// Virtual Base Class for Sensor
class SensorBase {
  public:
    SensorBase(int32_t sensorHandle, ISensorsEventCallback* callback, SensorType type);
    virtual ~SensorBase();
    const SensorInfo& getSensorInfo() const;
    virtual void batch(int32_t samplingPeriodNs) = 0;
    virtual void activate(bool enable) = 0;
    virtual Result flush();
    void setOperationMode(OperationMode mode);
    bool supportsDataInjection() const;
    Result injectEvent(const Event& event);

  protected:
    bool isWakeUpSensor();
    bool mIsEnabled;
    int64_t mSamplingPeriodNs;
    SensorInfo mSensorInfo;
    std::atomic_bool mStopThread;
    std::condition_variable mWaitCV;
    std::mutex mRunMutex;
    std::thread mRunThread;
    ISensorsEventCallback* mCallback;
    OperationMode mMode;
};

// HWSensorBase represents the actual physical sensor provided as the IIO device
class HWSensorBase : public SensorBase {
  public:
    static HWSensorBase* buildSensor(int32_t sensorHandle, ISensorsEventCallback* callback,
                                     struct iio_device_data& iio_data);
    ~HWSensorBase();
    void batch(int32_t samplingPeriodNs);
    void activate(bool enable);
    Result flush();
    struct iio_device_data mIioData;
    struct pollfd mPollFdIio;
    HWSensorBase(int32_t sensorHandle, ISensorsEventCallback* callback,
                 const struct iio_device_data& iio_data);

    std::vector<uint8_t> mSensorRawData;
    ssize_t mScanSize;
    int64_t mXMap, mYMap, mZMap;
  private:

    ssize_t calculateScanSize();
    void processScanData(uint8_t* data, Event* evt);
};

}  // namespace implementation
