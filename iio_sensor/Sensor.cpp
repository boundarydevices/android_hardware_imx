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

#include "Sensor.h"
#ifdef CONFIG_LEGACY_SENSOR
#include "LightSensor.h"
#include "PressureSensor.h"
#include "AnglvelSensor.h"
#include "AccMagSensor.h"
#else
#include "StepCounterSensor.h"
#endif

namespace nxp_sensors_subhal {

SensorBase::SensorBase(int32_t sensorHandle, ISensorsEventCallback* callback, SensorType type)
    : mIsEnabled(false), mSamplingPeriodNs(0), mCallback(callback), mMode(OperationMode::NORMAL) {
    mSensorInfo.type = type;
    mSensorInfo.sensorHandle = sensorHandle;
    mSensorInfo.vendor = "nxp";
    mSensorInfo.version = 1;
    mSensorInfo.fifoReservedEventCount = 0;
    mSensorInfo.fifoMaxEventCount = 100;
    mSensorInfo.requiredPermission = "";
    mSensorInfo.flags = SensorFlagBits::DATA_INJECTION |
                 SensorFlagBits::CONTINUOUS_MODE;
    switch (type) {
        case SensorType::ACCELEROMETER:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_ACCELEROMETER;
            break;
        case SensorType::TEMPERATURE:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_TEMPERATURE;
            break;
        case SensorType::MAGNETIC_FIELD:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_MAGNETIC_FIELD;
            break;
        case SensorType::PRESSURE:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_PRESSURE;
            break;
        case SensorType::GYROSCOPE:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_GYROSCOPE;
            break;
        case SensorType::LIGHT:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_LIGHT;
            break;
        case SensorType::STEP_COUNTER:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_STEP_COUNTER;
            break;
        default:
            ALOGE("unsupported sensor type %d", type);
            break;
    }
}

SensorBase::~SensorBase() {
}

HWSensorBase::~HWSensorBase() {
}

const SensorInfo& SensorBase::getSensorInfo() const {
    return mSensorInfo;
}

void HWSensorBase::batch(int32_t samplingPeriodNs) {
    samplingPeriodNs =
            std::clamp(samplingPeriodNs, mSensorInfo.minDelay * 1000, mSensorInfo.maxDelay * 1000);
    if (mSamplingPeriodNs != samplingPeriodNs) {
        //TODO: currently we still not support batch, disable it here.
        //unsigned int sampling_frequency = ns_to_frequency(samplingPeriodNs);
        //int i = 0;
        //mSamplingPeriodNs = samplingPeriodNs;
        //std::vector<double>::iterator low =
        //        std::lower_bound(mIioData.sampling_freq_avl.begin(),
        //                         mIioData.sampling_freq_avl.end(), sampling_frequency);
        //i = low - mIioData.sampling_freq_avl.begin();
        //set_sampling_frequency(mIioData.sysfspath, mIioData.sampling_freq_avl[i]);
        // Wake up the 'run' thread to check if a new event should be generated now
        mWaitCV.notify_all();
    }
}

void HWSensorBase::activate(bool enable) {
    std::unique_lock<std::mutex> lock(mRunMutex);
    if (mIsEnabled != enable) {
        mIsEnabled = enable;
        enable_sensor(mIioData.sysfspath, enable);
        mWaitCV.notify_all();
    }
}

Result SensorBase::flush() {
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

Result HWSensorBase::flush() {
    SensorBase::flush();
    return Result::OK;
}

template <size_t N>
static float getChannelData(const std::array<float, N>& channelData, int64_t map, bool negate) {
    return negate ? -channelData[map] : channelData[map];
}

void HWSensorBase::processScanData(uint8_t* data, Event* evt) {
    std::array<float, NUM_OF_DATA_CHANNELS> channelData;
    unsigned int chanIdx;
    evt->sensorHandle = mSensorInfo.sensorHandle;
    evt->sensorType = mSensorInfo.type;
    for (auto i = 0u; i < mIioData.channelInfo.size(); i++) {
        chanIdx = mIioData.channelInfo[i].index;

        const int64_t val =
                *reinterpret_cast<int64_t*>(data + chanIdx * mIioData.channelInfo[i].storage_bytes);
        // If the channel index is the last, it is timestamp
        // else it is sensor data
        if (chanIdx == mIioData.channelInfo.size() - 1) {
            evt->timestamp = val;
        } else {
            channelData[chanIdx] = static_cast<float>(val) * mIioData.scale;
        }
    }
}

bool SensorBase::isWakeUpSensor() {
    return mSensorInfo.flags & static_cast<uint32_t>(SensorFlagBits::WAKE_UP);
}

void SensorBase::setOperationMode(OperationMode mode) {
    std::unique_lock<std::mutex> lock(mRunMutex);
    if (mMode != mode) {
        mMode = mode;
        mWaitCV.notify_all();
    }
}

bool SensorBase::supportsDataInjection() const {
    return mSensorInfo.flags & static_cast<uint32_t>(SensorFlagBits::DATA_INJECTION);
}

Result SensorBase::injectEvent(const Event& event) {
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

ssize_t HWSensorBase::calculateScanSize() {
    ssize_t numBytes = 0;
    for (auto i = 0u; i < mIioData.channelInfo.size(); i++) {
        numBytes += mIioData.channelInfo[i].storage_bytes;
    }
    return numBytes;
}

static status_t checkIIOData(const struct iio_device_data& iio_data) {
    status_t ret = android::OK;
    for (auto i = 0u; i < iio_data.channelInfo.size(); i++) {
        if (iio_data.channelInfo[i].index > NUM_OF_DATA_CHANNELS) return android::BAD_VALUE;
    }
    return ret;
}

HWSensorBase* HWSensorBase::buildSensor(int32_t sensorHandle, ISensorsEventCallback* callback,
                                        struct iio_device_data& iio_data) {
    if (checkIIOData(iio_data) != android::OK) {
        ALOGE("IIO channel index of the sensor %s  is invalid", iio_data.name.c_str());
        return nullptr;
    }

#ifdef CONFIG_LEGACY_SENSOR
    if (iio_data.type == SensorType::LIGHT)
        return new LightSensor(sensorHandle, callback, iio_data);
    else if (iio_data.type == SensorType::PRESSURE)
        return new PressureSensor(sensorHandle, callback, iio_data);
    else if (iio_data.type == SensorType::TEMPERATURE)
        return new PressureSensor(sensorHandle, callback, iio_data);
    else if (iio_data.type == SensorType::ACCELEROMETER)
        return new AccMagSensor(sensorHandle, callback, iio_data);
    else if (iio_data.type == SensorType::MAGNETIC_FIELD)
        return new AccMagSensor(sensorHandle, callback, iio_data);
    else if (iio_data.type == SensorType::GYROSCOPE)
        return new AnglvelSensor(sensorHandle, callback, iio_data);
#endif
#ifdef CONFIG_SENSOR_PEDOMETER
    if (iio_data.type == SensorType::STEP_COUNTER)
        return new StepCounterSensor(sensorHandle, callback, iio_data);
#endif
    return nullptr;
}

HWSensorBase::HWSensorBase(int32_t sensorHandle, ISensorsEventCallback* callback,
                           const struct iio_device_data& data)
    : SensorBase(sensorHandle, callback, data.type) {
    mSensorInfo.flags |= SensorFlagBits::CONTINUOUS_MODE;
    mSensorInfo.name = data.name;
    mSensorInfo.resolution = data.resolution;
    mSensorInfo.maxRange = data.max_range * data.scale;
    mSensorInfo.power =
            (data.power_microwatts / 1000.f) / SENSOR_VOLTAGE_DEFAULT;  // converting uW to mA
    mIioData = data;
    unsigned int max_sampling_frequency = 0;
    unsigned int min_sampling_frequency = UINT_MAX;
    for (auto i = 0u; i < data.sampling_freq_avl.size(); i++) {
        if (max_sampling_frequency < data.sampling_freq_avl[i])
            max_sampling_frequency = data.sampling_freq_avl[i];
        if (min_sampling_frequency > data.sampling_freq_avl[i])
            min_sampling_frequency = data.sampling_freq_avl[i];
    }
    mSensorInfo.minDelay = frequency_to_us(max_sampling_frequency);
    mSensorInfo.maxDelay = frequency_to_us(min_sampling_frequency);
    mScanSize = calculateScanSize();
    mPollFdIio.events = POLLIN;
    mPollFdIio.revents = 0;
    mSensorRawData.resize(mScanSize);
}

}  // namespace implementation
