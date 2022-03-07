/*
 * Copyright 2021 NXP.
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

#define LOG_TAG "StepCounterSensor"

#include "StepCounterSensor.h"
#include "iio_utils.h"
#include <hardware/sensors.h>
#include <log/log.h>
#include <utils/SystemClock.h>
#include <cmath>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace subhal {
namespace implementation {

StepCounterSensor::StepCounterSensor(int32_t sensorHandle, ISensorsEventCallback* callback,
               struct iio_device_data& iio_data,
			   const std::optional<std::vector<Configuration>>& config)
	: HWSensorBase(sensorHandle, callback, iio_data, config)  {
    // no power_microwatts sys node, so mSensorInfo.power fake the default one.
    mSensorInfo.power = 0.001f;
    mSensorInfo.flags |= V1_0::SensorFlagBits::DATA_INJECTION | V1_0::SensorFlagBits::ON_CHANGE_MODE;

    std::string period_file;
    period_file = iio_data.sysfspath + "/events/in_steps_change_period";
    mSensorInfo.maxDelay = 500;
    mSensorInfo.minDelay = 3600000;
    mSysfspath = iio_data.sysfspath;

    mRunThread = std::thread(std::bind(&StepCounterSensor::run, this));
}

StepCounterSensor::~StepCounterSensor() {
    {
        std::unique_lock<std::mutex> lock(mRunMutex);
        mStopThread = true;
        mIsEnabled = false;
        mWaitCV.notify_all();
    }
    mRunThread.join();
}

void StepCounterSensor::activate(bool enable) {
    std::unique_lock<std::mutex> lock(mRunMutex);
    std::string buffer_path;
    if (mIsEnabled != enable) {
        buffer_path = "/dev/iio:device";
        buffer_path.append(std::to_string(mIioData.iio_dev_num));
        if (enable) {
            mPollFdIio.fd = open(buffer_path.c_str(), O_RDONLY | O_NONBLOCK);
            if (mPollFdIio.fd < 0) {
                ALOGI("Failed to open iio char device (%s).",  buffer_path.c_str());
            }
        } else {
            close(mPollFdIio.fd);
            mPollFdIio.fd = -1;
        }
        mIsEnabled = enable;
        enable_step_sensor(mIioData.sysfspath, enable);
        mWaitCV.notify_all();
    }
}

void StepCounterSensor::processScanData(Event* evt) {
    unsigned int stepcounter;
    evt->sensorHandle = mSensorInfo.sensorHandle;
    evt->sensorType = mSensorInfo.type;
    get_sensor_stepcounter(mSysfspath, &stepcounter);
    evt->timestamp = get_timestamp();
    evt->u.scalar = stepcounter;
}

bool StepCounterSensor::supportsDataInjection() const {
    return mSensorInfo.flags & static_cast<uint32_t>(V1_0::SensorFlagBits::DATA_INJECTION);
}

Result StepCounterSensor::injectEvent(const Event& event) {
    Result result = Result::OK;
    if (event.sensorType == SensorType::ADDITIONAL_INFO) {
        // When in OperationMode::NORMAL, SensorType::ADDITIONAL_INFO is used to push operation
        // environment data into the device.
    } else if (!supportsDataInjection()) {
        result = Result::INVALID_OPERATION;
    } else if (mMode == OperationMode::DATA_INJECTION) {
        mCallback->postEvents(std::vector<Event>{event}, isWakeUpSensor());
    } else {
        result = Result::BAD_VALUE;
    }
    return result;
}

void StepCounterSensor::setOperationMode(OperationMode mode) {
    std::unique_lock<std::mutex> lock(mRunMutex);
    if (mMode != mode) {
        mMode = mode;
        mWaitCV.notify_all();
    }
}

void StepCounterSensor::run() {
    int err;
    Event event;
    std::vector<Event> events;
    while (!mStopThread) {
        if (!mIsEnabled || mMode == OperationMode::DATA_INJECTION) {
            std::unique_lock<std::mutex> runLock(mRunMutex);
            mWaitCV.wait(runLock, [&] {
                return ((mIsEnabled && mMode == OperationMode::NORMAL) || mStopThread);
            });
        } else {
           err = poll(&mPollFdIio, 1, 500);
            if (err <= 0) {
                ALOGI("Sensor %s poll returned %d", mIioData.name.c_str(), err);
            }

            events.clear();
            processScanData(&event);
            events.push_back(event);
            mCallback->postEvents(events, isWakeUpSensor());
        }
    }
}

}  // namespace implementation
}  // namespace subhal
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
