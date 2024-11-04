/*
 * Copyright 2024 NXP.
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

#include "l3g4200dSensor.h"

namespace nxp_sensors_subhal {

l3g4200dSensor::l3g4200dSensor(int32_t sensorHandle, ISensorsEventCallback* callback,
                             struct iio_device_data& iio_data,
                             const std::optional<std::vector<Configuration>>& config)
      : HWSensorBase(sensorHandle, callback, iio_data, config) {
    // no power_microwatts sys node, so mSensorInfo.power fake the default one.
    mSensorInfo.power = 0.001f;
    std::string freq_file = iio_data.sysfspath + "/sampling_frequency_available";
    get_sampling_frequency_available(freq_file, &iio_data.sampling_freq_avl);

    unsigned int max_sampling_frequency = 0;
    unsigned int min_sampling_frequency = UINT_MAX;
    for (auto i = 0u; i < iio_data.sampling_freq_avl.size(); i++) {
        max_sampling_frequency = max_sampling_frequency < iio_data.sampling_freq_avl[i]
                ? iio_data.sampling_freq_avl[i]
                : max_sampling_frequency;
        min_sampling_frequency = min_sampling_frequency > iio_data.sampling_freq_avl[i]
                ? iio_data.sampling_freq_avl[i]
                : min_sampling_frequency;
    }

    mSensorInfo.minDelay = frequency_to_us(max_sampling_frequency);
    mSensorInfo.maxDelay = frequency_to_us(min_sampling_frequency);
    mSysfspath = iio_data.sysfspath;
    mIioData = iio_data;
    mRunThread = std::thread(std::bind(&l3g4200dSensor::run, this));
}

l3g4200dSensor::~l3g4200dSensor() {
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

void l3g4200dSensor::batch(int32_t samplingPeriodNs) {
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

bool l3g4200dSensor::supportsDataInjection() const {
    return mSensorInfo.flags & static_cast<uint32_t>(SensorFlagBits::DATA_INJECTION);
}

Result l3g4200dSensor::injectEvent(const Event& event) {
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

void l3g4200dSensor::setOperationMode(OperationMode mode) {
    std::unique_lock<std::mutex> lock(mRunMutex);
    if (mMode != mode) {
        mMode = mode;
        mWaitCV.notify_all();
    }
}

bool l3g4200dSensor::isWakeUpSensor() {
    return mSensorInfo.flags & static_cast<uint32_t>(SensorFlagBits::WAKE_UP);
}

Result l3g4200dSensor::flush() {
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

template <size_t N>
static float getChannelData(const std::array<float, N>& channelData, int64_t map, bool negate) {
    return negate ? -channelData[map] : channelData[map];
}

void l3g4200dSensor::processScanData(char* data, Event* evt) {
    unsigned int i, j;
    evt->sensorHandle = mSensorInfo.sensorHandle;
    evt->sensorType = mSensorInfo.type;
    char* channel_data;
    unsigned int chanIdx;
    int64_t sign_mask;
    int64_t value_mask;
    std::array<float, NUM_OF_DATA_CHANNELS> channelData;
    int64_t val;

    for (i = 0; i < mIioData.channelInfo.size(); i++) {
        chanIdx = mIioData.channelInfo[i].index;
        channel_data = data;
        val = 0;

        for (j = 0; j < mIioData.channelInfo.size(); j++) {
            if (chanIdx <= mIioData.channelInfo[j].index)
                continue;
            else {
                channel_data += mIioData.channelInfo[j].storage_bytes;
            }
        }
        if (mIioData.channelInfo[i].big_endian)
            for (int k = 0; k < mIioData.channelInfo[i].storage_bytes; k++)
                val = (val << 8) | channel_data[k];
        else
            for (int k = mIioData.channelInfo[i].storage_bytes - 1; k >= 0; k--)
                val = (val << 8) | channel_data[k];

        val = (val >> mIioData.channelInfo[i].shift) & (~0ULL >> mIioData.channelInfo[i].shift);
        if (!mIioData.channelInfo[i].sign)
            channelData[chanIdx] = (int64_t)val;
        else {
            switch (mIioData.channelInfo[i].bits_used) {
                case 0 ... 1:
                    channelData[chanIdx] = 0;
                    break;
                case 8:
                    channelData[chanIdx] = (int64_t)(int8_t)val;
                    break;
                case 16:
                    channelData[chanIdx] = (int64_t)(int16_t)val;
                    break;
                case 32:
                    channelData[chanIdx] = (int64_t)(int32_t)val;
                    break;
                case 64:
                    channelData[chanIdx] = (int64_t)val;
                    break;
                default:
                    sign_mask = 1 << (mIioData.channelInfo[i].bits_used - 1);
                    value_mask = sign_mask - 1;
                    if (val & sign_mask)
                        channelData[chanIdx] = -((~val & value_mask) +
                                                 1); /* Negative value: return 2-complement */
                    else
                        channelData[chanIdx] = (int64_t)val; /* Positive value */
            }
        }
    }

    // l3g4200dSensor scale value is 0.000153.
    evt->u.vec3.x = getChannelData(channelData, mXMap, false) * 0.000153;
    evt->u.vec3.y = getChannelData(channelData, mYMap, false) * 0.000153;
    evt->u.vec3.z = getChannelData(channelData, mZMap, false) * 0.000153;
    evt->timestamp = get_timestamp();
}

void l3g4200dSensor::setupSysfsTrigger(const std::string& device_dir, uint8_t dev_num, bool enable) {
    add_trigger(device_dir, dev_num, enable);
}

void l3g4200dSensor::setupHrtimerTrigger(const std::string& device_dir, uint8_t dev_num,
                                        bool enable) {
    add_hrtimer_trigger(device_dir, dev_num, enable);
}

void l3g4200dSensor::activate(bool enable) {
    std::unique_lock<std::mutex> lock(mRunMutex);
    std::string buffer_path;
    if (mIsEnabled != enable) {
        buffer_path = "/dev/iio:device";
        buffer_path.append(std::to_string(mIioData.iio_dev_num));
        if (enable) {
            mPollFdIio.fd = open(buffer_path.c_str(), O_RDONLY | O_NONBLOCK);
            if (mPollFdIio.fd < 0) {
                ALOGI("Failed to open iio char device (%s).", buffer_path.c_str());
            } else {
                if (GetProperty(kTriggerType, "") == "hrtimer_trigger")
                    setupHrtimerTrigger(mIioData.sysfspath, mIioData.iio_dev_num, enable);
                else if (GetProperty(kTriggerType, "") == "sysfs_trigger")
                    setupSysfsTrigger(mIioData.sysfspath, mIioData.iio_dev_num, enable);
                enable_sensor(mIioData.sysfspath, enable);
                mWaitCV.notify_all();
            }
        } else {
            close(mPollFdIio.fd);
            mPollFdIio.fd = -1;
        }

        mIsEnabled = enable;
    }
}

void l3g4200dSensor::run() {
    int read_size;
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
            if (GetProperty(kTriggerType, "") == "sysfs_trigger")
                trigger_data(mIioData.iio_dev_num);
            err = poll(&mPollFdIio, 1, -1);
            if (err <= 0) {
                ALOGV("Sensor %s poll returned %d", mIioData.name.c_str(), err);
                continue;
            }
            char readbuf[16];
            if (mPollFdIio.revents & POLLIN) {
                read_size = pread(mPollFdIio.fd, readbuf, 16, 0);
                if (read_size <= 0) {
                    ALOGE("%s: Failed to read data from iio char device. %d", mIioData.name.c_str(),
                          errno);
                    continue;
                }
                events.clear();
                processScanData(readbuf, &event);
                events.push_back(event);
                mCallback->postEvents(events, isWakeUpSensor());
            }
        }
    }
}

} // namespace nxp_sensors_subhal
