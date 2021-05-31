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

#define LOG_TAG "AccMagSensor"

#include "AccMagSensor.h"
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

AccMagSensor::AccMagSensor(int32_t sensorHandle, ISensorsEventCallback* callback,
               struct iio_device_data& iio_data,
			   const std::optional<std::vector<Configuration>>& config)
	: HWSensorBase(sensorHandle, callback, iio_data, config)  {
    std::string freq_file_name;
    if (iio_data.type == SensorType::ACCELEROMETER) {
        mSensorInfo.power = 0.3f;
        freq_file_name = "/in_accel_sampling_frequency_available";
    } else if (iio_data.type == SensorType::MAGNETIC_FIELD) {
        mSensorInfo.power = 0.5f;
        freq_file_name = "/in_magn_sampling_frequency_available";
    }

    std::string freq_file;
    freq_file = iio_data.sysfspath + freq_file_name;
    get_sampling_frequency_available(freq_file, &iio_data.sampling_freq_avl);

    unsigned int max_sampling_frequency = 0;
    unsigned int min_sampling_frequency = UINT_MAX;
    for (auto i = 0u; i < iio_data.sampling_freq_avl.size(); i++) {
        if (max_sampling_frequency < iio_data.sampling_freq_avl[i])
            max_sampling_frequency = iio_data.sampling_freq_avl[i];
        if (min_sampling_frequency > iio_data.sampling_freq_avl[i])
            min_sampling_frequency = iio_data.sampling_freq_avl[i];
    }

    mSensorInfo.minDelay = frequency_to_us(max_sampling_frequency);
    mSensorInfo.maxDelay = frequency_to_us(min_sampling_frequency);
    mSysfspath = iio_data.sysfspath;
    mRunThread = std::thread(std::bind(&AccMagSensor::run, this));
}

AccMagSensor::~AccMagSensor() {
    // Ensure that lock is unlocked before calling mRunThread.join() or a
    // deadlock will occur.
    {
        std::unique_lock<std::mutex> lock(mRunMutex);
        mStopThread = true;
        mIsEnabled = false;
        mWaitCV.notify_all();
    }
    mRunThread.join();
}

void AccMagSensor::activate(bool enable) {
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

void AccMagSensor::processScanData(Event* evt) {
    iio_acc_mac_data data;
    evt->sensorHandle = mSensorInfo.sensorHandle;
    evt->sensorType = mSensorInfo.type;
    if (mSensorInfo.type == SensorType::ACCELEROMETER)
        get_sensor_acc(mSysfspath, &data);
    else if(mSensorInfo.type == SensorType::MAGNETIC_FIELD)
        get_sensor_mag(mSysfspath, &data);

    evt->u.vec3.x  = data.x_raw;
    evt->u.vec3.y  = data.y_raw;
    evt->u.vec3.z  = data.z_raw;
    evt->timestamp = get_timestamp();
}

void AccMagSensor::run() {
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
                events.clear();
                processScanData(&event);
                events.push_back(event);
                mCallback->postEvents(events, isWakeUpSensor());
            }
        }
    }
}

}  // namespace implementation
}  // namespace subhal
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
