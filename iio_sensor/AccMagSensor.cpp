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

#include "AccMagSensor.h"

namespace nxp_sensors_subhal {

AccMagSensor::AccMagSensor(int32_t sensorHandle, ISensorsEventCallback* callback,
               struct iio_device_data& iio_data)
	: HWSensorBase(sensorHandle, callback, iio_data)  {
    // no power_microwatts/resolution sys node, so mSensorInfo.power/resolution fake the default one,
    // no maxRange sys node, so fake maxRange, which is set according to the CTS requirement.
    if (iio_data.type == SensorType::ACCELEROMETER) {
        mSensorInfo.power = 0.001f;
        mSensorInfo.maxRange = 39.20f;
        mSensorInfo.resolution = 0.01f;
        freq_file_name = "/in_accel_sampling_frequency_available";
    } else if (iio_data.type == SensorType::MAGNETIC_FIELD) {
        mSensorInfo.power = 0.001f;
        mSensorInfo.maxRange = 900.00f;
        mSensorInfo.resolution = 0.01f;
        freq_file_name = "/in_magn_sampling_frequency_available";
    }

    std::string freq_file = iio_data.sysfspath + freq_file_name;
    get_sampling_frequency_available(freq_file, &iio_data.sampling_freq_avl);

    unsigned int max_sampling_frequency = 0;
    unsigned int min_sampling_frequency = UINT_MAX;
    for (auto i = 0u; i < iio_data.sampling_freq_avl.size(); i++) {
        max_sampling_frequency = max_sampling_frequency < iio_data.sampling_freq_avl[i]
                                 ? iio_data.sampling_freq_avl[i] : max_sampling_frequency;
        min_sampling_frequency = min_sampling_frequency > iio_data.sampling_freq_avl[i]
                                 ? iio_data.sampling_freq_avl[i] : min_sampling_frequency;
    }
    mSensorInfo.minDelay = frequency_to_us(max_sampling_frequency);
    mSensorInfo.maxDelay = frequency_to_us(min_sampling_frequency);
    mSysfspath = iio_data.sysfspath;
    mIioData = iio_data;

    if (mIioData.type == SensorType::ACCELEROMETER) {
        static const char* IIO_ACC_X_RAW = "in_accel_x_raw";
        static const char* IIO_ACC_Y_RAW = "in_accel_y_raw";
        static const char* IIO_ACC_Z_RAW = "in_accel_z_raw";

        std::string x_filename = mSysfspath + "/" + IIO_ACC_X_RAW;
        std::string y_filename = mSysfspath + "/" + IIO_ACC_Y_RAW;
        std::string z_filename = mSysfspath + "/" + IIO_ACC_Z_RAW;

        fdx = unique_fd(open(x_filename.c_str(), O_RDONLY));
        fdy = unique_fd(open(y_filename.c_str(), O_RDONLY));
        fdz = unique_fd(open(z_filename.c_str(), O_RDONLY));
    }

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

void AccMagSensor::batch(int32_t samplingPeriodNs) {
    samplingPeriodNs =
            std::clamp(samplingPeriodNs, mSensorInfo.minDelay * 1000, mSensorInfo.maxDelay * 1000);
    if (mSamplingPeriodNs != samplingPeriodNs) {
        unsigned int sampling_frequency = ns_to_frequency(samplingPeriodNs);
        int i = 0;
        mSamplingPeriodNs = samplingPeriodNs;

        std::vector<double>::iterator low =
                std::lower_bound(mIioData.sampling_freq_avl.begin(),
                                 mIioData.sampling_freq_avl.end(), sampling_frequency);
        i = low - mIioData.sampling_freq_avl.begin();
        set_sampling_frequency(mIioData.sysfspath, mIioData.sampling_freq_avl[i]);
        // Wake up the 'run' thread to check if a new event should be generated now
        mWaitCV.notify_all();
    }
}

bool AccMagSensor::supportsDataInjection() const {
    return mSensorInfo.flags & static_cast<uint32_t>(SensorFlagBits::DATA_INJECTION);
}

Result AccMagSensor::injectEvent(const Event& event) {
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

void AccMagSensor::setOperationMode(OperationMode mode) {
    std::unique_lock<std::mutex> lock(mRunMutex);
    if (mMode != mode) {
        mMode = mode;
        mWaitCV.notify_all();
    }
}

bool AccMagSensor::isWakeUpSensor() {
    return mSensorInfo.flags & static_cast<uint32_t>(SensorFlagBits::WAKE_UP);
}

Result AccMagSensor::flush() {
    // Only generate a flush complete event if the sensor is enabled and if the sensor is not a
    // one-shot sensor.
    if (!mIsEnabled || (mSensorInfo.flags & static_cast<uint32_t>(SensorFlagBits::ONE_SHOT_MODE))) {
        return Result::BAD_VALUE;
    }

    // Note: If a sensor supports batching, write all of the currently batched events for the sensor
    // to the Event FMQ prior to writing the flush complete event.
    Event ev;
    ev.sensorHandle = mSensorInfo.sensorHandle;
    ev.sensorType = SensorType::META_DATA;
    ev.u.meta.what = MetaDataEventType::META_DATA_FLUSH_COMPLETE;
    std::vector<Event> evs{ev};
    mCallback->postEvents(evs, isWakeUpSensor());
    return Result::OK;
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
    if (mSensorInfo.type == SensorType::ACCELEROMETER) {
        char bufx[64], bufy[64], bufz[64];

        read(fdx, bufx, sizeof(bufx));
        lseek(fdx,0L,SEEK_SET);
        read(fdy, bufy, sizeof(bufy));
        lseek(fdy,0L,SEEK_SET);
        read(fdz, bufz, sizeof(bufz));
        lseek(fdz,0L,SEEK_SET);

        // scale sys node is not valid, to meet xTS required range, multiply raw data with 0.005.
        evt->u.vec3.x  = atoi(bufx) * 0.000244;
        evt->u.vec3.y  = atoi(bufy) * 0.000244;
        evt->u.vec3.z  = atoi(bufz) * 0.000244;
    } else if(mSensorInfo.type == SensorType::MAGNETIC_FIELD) {
        get_sensor_mag(mSysfspath, &data);
        // 0.000244 is read from sys node in_magn_scale.
        evt->u.vec3.x  = data.x_raw * 0.000244;
        evt->u.vec3.y  = data.y_raw * 0.000244;
        evt->u.vec3.z  = data.z_raw * 0.000244;
    }
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
                ALOGV("Sensor %s poll returned %d", mIioData.name.c_str(), err);
            }
            events.clear();
            processScanData(&event);
            events.push_back(event);
            mCallback->postEvents(events, isWakeUpSensor());
        }
    }
}

}  // namespace nxp_sensors_subhal
