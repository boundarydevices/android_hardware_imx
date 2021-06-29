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

#define LOG_TAG "AnglvelSensor"

#include "AnglvelSensor.h"
#include "iio_utils.h"
#include <hardware/sensors.h>
#include <log/log.h>
#include <utils/SystemClock.h>
#include <cmath>
#include <sys/socket.h>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace subhal {
namespace implementation {

AnglvelSensor::AnglvelSensor(int32_t sensorHandle, ISensorsEventCallback* callback,
               struct iio_device_data& iio_data,
			   const std::optional<std::vector<Configuration>>& config)
	: HWSensorBase(sensorHandle, callback, iio_data, config)  {
    // no power_microwatts sys node, so mSensorInfo.power fake the default one.
    mSensorInfo.power = 0.001f;
    std::string freq_file;
    freq_file = iio_data.sysfspath + "/sampling_frequency_available";
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
    mRunThread = std::thread(std::bind(&AnglvelSensor::run, this));
}

AnglvelSensor::~AnglvelSensor() {
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

void AnglvelSensor::batch(int32_t samplingPeriodNs) {
    samplingPeriodNs =
            std::clamp(samplingPeriodNs, mSensorInfo.minDelay * 1000, mSensorInfo.maxDelay * 1000);
    if (mSamplingPeriodNs != samplingPeriodNs) {
        unsigned int sampling_frequency = ns_to_frequency(samplingPeriodNs);
        int i = 0;
        mSamplingPeriodNs = samplingPeriodNs;
        std::string freq_file;
        freq_file = mIioData.sysfspath + "/sampling_frequency_available";
        get_sampling_frequency_available(freq_file, &mIioData.sampling_freq_avl);
        std::vector<double>::iterator low =
                std::lower_bound(mIioData.sampling_freq_avl.begin(),
                                 mIioData.sampling_freq_avl.end(), sampling_frequency);
        i = low - mIioData.sampling_freq_avl.begin();
        set_sampling_frequency(mIioData.sysfspath, mIioData.sampling_freq_avl[i]);
        // Wake up the 'run' thread to check if a new event should be generated now
        mWaitCV.notify_all();
    }
}

bool AnglvelSensor::supportsDataInjection() const {
    return mSensorInfo.flags & static_cast<uint32_t>(V1_0::SensorFlagBits::DATA_INJECTION);
}

Result AnglvelSensor::injectEvent(const Event& event) {
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

void AnglvelSensor::setOperationMode(OperationMode mode) {
    std::unique_lock<std::mutex> lock(mRunMutex);
    if (mMode != mode) {
        mMode = mode;
        mWaitCV.notify_all();
    }
}

bool AnglvelSensor::isWakeUpSensor() {
    return mSensorInfo.flags & static_cast<uint32_t>(V1_0::SensorFlagBits::WAKE_UP);
}

Result AnglvelSensor::flush() {
    // Only generate a flush complete event if the sensor is enabled and if the sensor is not a
    // one-shot sensor.
    if (!mIsEnabled || (mSensorInfo.flags & static_cast<uint32_t>(V1_0::SensorFlagBits::ONE_SHOT_MODE))) {
        return Result::BAD_VALUE;
    }

    // Note: If a sensor supports batching, write all of the currently batched events for the sensor
    // to the Event FMQ prior to writing the flush complete event.

    Event ev;
    ev.sensorHandle = mSensorInfo.sensorHandle;
    ev.sensorType = SensorType::META_DATA;
    ev.u.meta.what = V1_0::MetaDataEventType::META_DATA_FLUSH_COMPLETE;
    std::vector<Event> evs{ev};
    mCallback->postEvents(evs, isWakeUpSensor());
    return Result::OK;
}

template <size_t N>
static float getChannelData(const std::array<float, N>& channelData, int64_t map, bool negate) {
    return negate ? -channelData[map] : channelData[map];
}

void AnglvelSensor::processScanData(char* data, Event* evt) {
    unsigned int i, j, k;
    evt->sensorHandle = mSensorInfo.sensorHandle;
    evt->sensorType = mSensorInfo.type;
    char *channel_data;
    unsigned int chanIdx;
    uint64_t sign_mask;
    uint64_t value_mask;
    std::array<float, NUM_OF_DATA_CHANNELS> channelData;
    int64_t val;

    for(i = 0; i < mIioData.channelInfo.size(); i++) {
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
            for (k=0; k<mIioData.channelInfo[i].storage_bytes; k++)
                val = (val << 8) | channel_data[k];
        else
            for (k=mIioData.channelInfo[i].storage_bytes -1; k>=0; k--)
                val = (val << 8) | channel_data[k];

        val = (val >> mIioData.channelInfo[i].shift) & (~0ULL >> mIioData.channelInfo[i].shift);
        if (!mIioData.channelInfo[i].sign)
            channelData[chanIdx] = (int64_t)val;
        else {
            switch(mIioData.channelInfo[i].bits_used) {
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
                sign_mask = 1 << (mIioData.channelInfo[i].bits_used-1);
                value_mask = sign_mask - 1;
                if (val & sign_mask)
                    channelData[chanIdx] = - ((~val & value_mask) + 1); /* Negative value: return 2-complement */
                else
                    channelData[chanIdx] = (int64_t)val;           /* Positive value */
            }
        }
    }

    // in_anglvel_scale value is 62.5, but to meet xTS required range, multiply data with 1/625.
    evt->u.vec3.x = getChannelData(channelData, mXMap, true) / 625;
    evt->u.vec3.y = getChannelData(channelData, mYMap, true) / 625;
    evt->u.vec3.z = getChannelData(channelData, mZMap, true) / 625;
    evt->timestamp = get_timestamp();
}

void AnglvelSensor::setupSysfsTrigger(const std::string& device_dir, uint8_t dev_num, bool enable) {
    add_trigger(device_dir, dev_num, enable);
}

void AnglvelSensor::setupHrtimerTrigger(const std::string& device_dir, uint8_t dev_num, bool enable) {
    add_hrtimer_trigger(device_dir, dev_num, enable);
}

void AnglvelSensor::activate(bool enable) {
    std::unique_lock<std::mutex> lock(mRunMutex);
    std::string buffer_path;
    if (mIsEnabled != enable) {
        buffer_path = "/dev/iio:device";
        buffer_path.append(std::to_string(mIioData.iio_dev_num));
        if (enable) {
            mPollFdIio.fd = open(buffer_path.c_str(), O_RDONLY | O_NONBLOCK);
            if (mPollFdIio.fd < 0) {
                ALOGI("Failed to open iio char device (%s).",  buffer_path.c_str());
            } else {
                if (GetProperty(kTriggerType, "") == "hrtimer_trigger")
                    setupHrtimerTrigger(mIioData.sysfspath, mIioData.iio_dev_num, enable);
                else if(GetProperty(kTriggerType, "") == "sysfs_trigger")
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

void AnglvelSensor::run() {
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
            if(GetProperty(kTriggerType, "") == "sysfs_trigger")
                trigger_data(mIioData.iio_dev_num);
            err = poll(&mPollFdIio, 1, mSamplingPeriodNs/1000000);
            if (err <= 0) {
                ALOGE("Sensor %s poll returned %d", mIioData.name.c_str(), err);
                continue;
            }
            char readbuf[16];
            if (mPollFdIio.revents & POLLIN) {
                read_size = pread(mPollFdIio.fd, readbuf, 16, 0);
                if (read_size <= 0) {
                    ALOGE("%s: Failed to read data from iio char device. %d", mIioData.name.c_str(), errno);
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

}  // namespace implementation
}  // namespace subhal
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
