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
               struct iio_device_data& iio_data)
	: HWSensorBase(sensorHandle, callback, iio_data)  {
    // no power_microwatts sys node, so mSensorInfo.power fake the default one.
    mSensorInfo.power = 0.001f;

    // currently mpl3115 do not support sampling freq setting, fake one value
    // it's align with old sensor hal
    mSensorInfo.minDelay = 2500;
    mSensorInfo.maxDelay = 500000;
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

void PressureSensor::processScanData(char* data, Event* evt, int mChannelIndex) {
    unsigned int i, index = 0;
    if (mChannelIndex == 0) {
        evt->sensorHandle = 3;
        evt->sensorType = SensorType::PRESSURE;
    }
    if (mChannelIndex == 1) {
        evt->sensorHandle = 4;
        evt->sensorType = SensorType::TEMPERATURE;
    }

    char *channel_data = data;
    uint64_t sign_mask;
    uint64_t value_mask;

    int64_t val = 0;
    for (i =0; i < mIioData.channelInfo.size(); i++) {
        if (strstr(mIioData.channelInfo[i].name.c_str(), "pressure") && mChannelIndex == 0) {
            index = i;
            break;
        } else if (strstr(mIioData.channelInfo[i].name.c_str(), "temp") && mChannelIndex == 1) {
            index = i;
            break;
        }
    }

    for(i = 0; i < mIioData.channelInfo.size(); i++) {
        if (mIioData.channelInfo[index].index <= mIioData.channelInfo[i].index)
            continue;
        else {
            channel_data += mIioData.channelInfo[i].storage_bytes;
        }
    }

    if (mIioData.channelInfo[index].big_endian)
        for (i=0; i<mIioData.channelInfo[index].storage_bytes; i++)
            val = (val << 8) | channel_data[i];
    else
        for (i=mIioData.channelInfo[index].storage_bytes -1; i>=0; i--)
            val = (val << 8) | channel_data[i];

    val = (val >> mIioData.channelInfo[index].shift) & (~0ULL >> mIioData.channelInfo[index].shift);

    if (!mIioData.channelInfo[index].sign)
        evt->u.scalar = (int64_t)val;
    else {
        switch(mIioData.channelInfo[index].bits_used) {
                case 0 ... 1:
                    evt->u.scalar = 0;
                    break;
                case 8:
                    evt->u.scalar = (int64_t)(int8_t)val;
                    break;
                case 16:
                    evt->u.scalar = (int64_t)(int16_t)val;
                    break;
                case 32:
                    evt->u.scalar = (int64_t)(int32_t)val;
                    break;
                case 64:
                    evt->u.scalar = (int64_t)val;
                    break;
                default:
                    sign_mask = 1 << (mIioData.channelInfo[i].bits_used-1);
                    value_mask = sign_mask - 1;
                    if (val & sign_mask)
                        evt->u.scalar = - ((~val & value_mask) + 1); /* Negative value: return 2-complement */
                    else
                        evt->u.scalar = (int64_t)val;           /* Positive value */
         }
    }

    float scale;
    std::string scale_file;
    if (mChannelIndex == 0) {
        scale_file = mSysfspath + "/in_pressure_scale";
    } else if(mChannelIndex == 1) {
        scale_file = mSysfspath + "/in_temp_scale";
    }

    get_pressure_scale(scale_file, &scale);

    evt->u.scalar = scale * evt->u.scalar;

    // To meet CTS required range, multiply pressure scale with 10.
    if (mChannelIndex == 0)
        evt->u.scalar *= 10;

    int timestamp_offset = 0;
    for (auto i = 0u; i < mIioData.channelInfo.size(); i++) {
        if ((mIioData.channelInfo.size()-1) > mIioData.channelInfo[i].index)
            timestamp_offset += mIioData.channelInfo[i].storage_bytes;
    }

    const int64_t timestamp =
             *reinterpret_cast<int64_t*>(data + timestamp_offset * 8);

    if (timestamp == 0)
        evt->timestamp = get_timestamp();
    else
        evt->timestamp = timestamp;

}

void PressureSensor::setupSysfsTrigger(const std::string& device_dir, uint8_t dev_num, bool enable) {
    add_trigger(device_dir, dev_num, enable);
}

void PressureSensor::setupHrtimerTrigger(const std::string& device_dir, uint8_t dev_num, bool enable) {
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
                ALOGI("Failed to open iio char device (%s).",  buffer_path.c_str());
            else {
                if(GetProperty(kTriggerType, "") == "hrtimer_trigger")
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
            if(GetProperty(kTriggerType, "") == "sysfs_trigger")
                trigger_data(mIioData.iio_dev_num);
            err = poll(&mPollFdIio, 1, 50);
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
                processScanData(readbuf, &event, 0);
                events.push_back(event);
                processScanData(readbuf, &event, 1);
                events.push_back(event);

                mCallback->postEvents(events, isWakeUpSensor());
            }
        }
    }
}

}  // namespace nxp_sensors_subhal
