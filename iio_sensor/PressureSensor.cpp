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

#define LOG_TAG "PressureSensor"

#include "PressureSensor.h"
#include "iio_utils.h"
#include <hardware/sensors.h>
#include <log/log.h>
#include <utils/SystemClock.h>
#include <cmath>
#include <sys/socket.h>
#include <inttypes.h>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace subhal {
namespace implementation {

using ::android::hardware::sensors::V1_0::AdditionalInfoType;
using ::android::hardware::sensors::V1_0::MetaDataEventType;
using ::android::hardware::sensors::V1_0::SensorFlagBits;
using ::android::hardware::sensors::V1_0::SensorStatus;
using ::sensor::hal::configuration::V1_0::Location;
using ::sensor::hal::configuration::V1_0::Orientation;

PressureSensor::PressureSensor(int32_t sensorHandle, ISensorsEventCallback* callback,
               struct iio_device_data& iio_data,
			   const std::optional<std::vector<Configuration>>& config)
	: HWSensorBase(sensorHandle, callback, iio_data, config)  {
    mSensorInfo.power = 0.5f;

    // currently mpl3115 do not support sampling freq setting, fake one value
    // it's align with old sensor hal
    mSensorInfo.minDelay = 2500;
    mSensorInfo.maxDelay = 500000;
    mSysfspath = iio_data.sysfspath;
    if (iio_data.type == SensorType::PRESSURE) {
        mChannelIndex = 0;
    } else if(iio_data.type == SensorType::TEMPERATURE) {
        mChannelIndex = 1;
    }
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

void PressureSensor::processScanData(char* data, Event* evt) {
    unsigned int i, index = 0;
    evt->sensorHandle = mSensorInfo.sensorHandle;
    evt->sensorType = mSensorInfo.type;
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
        } else {
            close(mPollFdIio.fd);
            mPollFdIio.fd = -1;
        }

        mIsEnabled = enable;
        setupSysfsTrigger(mIioData.sysfspath, mIioData.iio_dev_num, enable);
        enable_sensor(mIioData.sysfspath, enable);
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
            trigger_data(mIioData.iio_dev_num);
            err = poll(&mPollFdIio, 1, 500);
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
