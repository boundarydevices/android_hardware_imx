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

#include "PressureSensor.h"

namespace nxp_sensors_subhal {

PressureSensor::PressureSensor(int32_t sensorHandle, ISensorsEventCallback* callback,
                               struct iio_device_data& iio_data,
                               const std::optional<std::vector<Configuration>>& config)
      : HWSensorBase(sensorHandle, callback, iio_data, config) {
    // no power_microwatts sys node, so mSensorInfo.power fake the default one.
    mSensorInfo.power = 0.001f;

    // currently mpl3115 do not support sampling freq setting, fake one value
    // it's align with old sensor hal
    mSensorInfo.minDelay = 2500;
    mSensorInfo.maxDelay = 500000;
    if (iio_data.type == SensorType::AMBIENT_TEMPERATURE)
        mSensorInfo.flags = SensorFlagBits::DATA_INJECTION | SensorFlagBits::ON_CHANGE_MODE;
    if (iio_data.type == SensorType::PRESSURE)
        mSensorInfo.flags = SensorFlagBits::DATA_INJECTION | SensorFlagBits::CONTINUOUS_MODE;

    mSensorHandle = sensorHandle;
    mSensorInfo.type = iio_data.type;
    mSysfspath = iio_data.sysfspath;
    mRunThread = std::thread(std::bind(&PressureSensor::run, this));
}

PressureSensor::~PressureSensor() {
    {
        std::unique_lock<std::mutex> lock(mRunMutex);
        mStopThread = true;
        mIsEnabled = false;
        mWaitCV.notify_all();
    }
    mRunThread.join();
}

template <size_t N>
static float getChannelData(const std::array<float, N>& channelData, int64_t map, bool negate) {
    return negate ? -channelData[map] : channelData[map];
}

void PressureSensor::processScanData(char* data, Event* evt) {
    unsigned int i, j;
    evt->sensorHandle = mSensorInfo.sensorHandle;
    evt->sensorType = mSensorInfo.type;
    char* channel_data;
    unsigned int chanIdx;
    int64_t sign_mask;
    int64_t value_mask;
    std::array<float, NUM_OF_DATA_CHANNELS> channelData;
    int64_t val;
    int shift_timestamp;

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

        // there is 2 bytes offset bewteen z and timestamp data.
        if (strstr(mIioData.channelInfo[i].name.c_str(), "timestamp"))
            shift_timestamp = 2;
        else
            shift_timestamp = 0;

        if (mIioData.channelInfo[i].big_endian)
            for (int k = shift_timestamp; k < mIioData.channelInfo[i].storage_bytes; k++)
                val = (val << 8) | channel_data[k];
        else
            for (int k = mIioData.channelInfo[i].storage_bytes + shift_timestamp - 1; k >= shift_timestamp; k--)
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

    float scale;
    std::string scale_file;
    if (mSensorInfo.type == SensorType::PRESSURE) {
        scale_file = mSysfspath + "/in_pressure_scale";
    } else if (mSensorInfo.type == SensorType::AMBIENT_TEMPERATURE) {
        scale_file = mSysfspath + "/in_temp_scale";
    }

    get_pressure_scale(scale_file, &scale);

    if (mSensorInfo.type == SensorType::PRESSURE) {
        // To meet CTS required range, multiply pressure scale with 10.
        evt->u.scalar = getChannelData(channelData, 0, false) * scale * 10;
    }
    if (mSensorInfo.type == SensorType::AMBIENT_TEMPERATURE)
        evt->u.scalar = getChannelData(channelData, 1, false) * scale;

    // TODO:
    // when sensor driver fix sampling frequency/timestamp mismatch issue,
    // plan to switch to get timestamp from channel data.
    // evt->timestamp = getChannelData(channelData, 2, false) * 0.001;
    evt->timestamp = get_timestamp();
}

void PressureSensor::setupSysfsTrigger(const std::string& device_dir, uint8_t dev_num,
                                       bool enable) {
    add_trigger(device_dir, dev_num, enable);
}

void PressureSensor::setupHrtimerTrigger(const std::string& device_dir, uint8_t dev_num,
                                         bool enable) {
    add_hrtimer_trigger(device_dir, dev_num, enable);
}

void PressureSensor::activate(bool enable) {
    std::unique_lock<std::mutex> lock(mRunMutex);
    std::string buffer_path;
    if (mIsEnabled != enable) {
        buffer_path = "/dev/iio:device";
        buffer_path.append(std::to_string(mIioData.iio_dev_num));
        if (enable) {
            mPollFdIio.fd = open(buffer_path.c_str(), O_RDONLY | O_NONBLOCK);
            if (mPollFdIio.fd < 0)
                ALOGI("Failed to open iio char device (%s).", buffer_path.c_str());
            else {
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

bool PressureSensor::supportsDataInjection() const {
    return mSensorInfo.flags & static_cast<uint32_t>(SensorFlagBits::DATA_INJECTION);
}

Result PressureSensor::injectEvent(const Event& event) {
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

void PressureSensor::setOperationMode(OperationMode mode) {
    std::unique_lock<std::mutex> lock(mRunMutex);
    if (mMode != mode) {
        mMode = mode;
        mWaitCV.notify_all();
    }
}

void PressureSensor::run() {
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
                ALOGE("Sensor %s poll returned %d", mIioData.name.c_str(), err);
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
