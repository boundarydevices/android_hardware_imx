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

#include "LightSensor.h"

namespace nxp_sensors_subhal {

LightSensor::LightSensor(int32_t sensorHandle, ISensorsEventCallback* callback,
               struct iio_device_data& iio_data)
	: HWSensorBase(sensorHandle, callback, iio_data)  {
    // no power_microwatts sys node, so mSensorInfo.power fake the default one.
    mSensorInfo.power = 0.001f;
    mSensorInfo.flags |= SensorFlagBits::DATA_INJECTION | SensorFlagBits::ON_CHANGE_MODE;

    std::string time_file = iio_data.sysfspath + "/in_illuminance_integration_time_available";
    get_sampling_time_available(time_file, &iio_data.sampling_time_avl);

    mSensorInfo.maxDelay =
              iio_data.sampling_time_avl[0] * 1000000;
    mSensorInfo.minDelay =
              iio_data.sampling_time_avl[iio_data.sampling_time_avl.size() - 1] * 1000000;
    mSysfspath = iio_data.sysfspath;

    mRunThread = std::thread(std::bind(&LightSensor::run, this));
}

LightSensor::~LightSensor() {
    {
        std::unique_lock<std::mutex> lock(mRunMutex);
        mStopThread = true;
        mIsEnabled = false;
        mWaitCV.notify_all();
    }
    mRunThread.join();
}

void LightSensor::activate(bool enable) {
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
        enable_sensor(mIioData.sysfspath, enable);
        mWaitCV.notify_all();
    }
}

void LightSensor::processScanData(Event* evt) {
    unsigned int light;
    evt->sensorHandle = mSensorInfo.sensorHandle;
    evt->sensorType = mSensorInfo.type;
    get_sensor_light(mSysfspath, &light);
    evt->timestamp = get_timestamp();
    evt->u.scalar = light;
}

bool LightSensor::supportsDataInjection() const {
    return mSensorInfo.flags & static_cast<uint32_t>(SensorFlagBits::DATA_INJECTION);
}

Result LightSensor::injectEvent(const Event& event) {
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

void LightSensor::setOperationMode(OperationMode mode) {
    std::unique_lock<std::mutex> lock(mRunMutex);
    if (mMode != mode) {
        mMode = mode;
        mWaitCV.notify_all();
    }
}

void LightSensor::run() {
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
            err = poll(&mPollFdIio, 1, 50);
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

}  // namespace nxp_sensors_subhal
