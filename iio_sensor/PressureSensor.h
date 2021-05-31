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

#ifndef ANDROID_HARDWARE_SENSORS_V2_0_PRESSURESENSOR_H
#define ANDROID_HARDWARE_SENSORS_V2_0_PRESSURESENSOR_H

#include <android/hardware/sensors/1.0/types.h>
#include <poll.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "iio_utils.h"
#include "Sensor.h"
#include "sensor_hal_configuration_V1_0.h"

#define NUM_OF_CHANNEL_SUPPORTED 4
// Subtract the timestamp channel to get the number of data channels
#define NUM_OF_DATA_CHANNELS NUM_OF_CHANNEL_SUPPORTED - 1

using ::android::hardware::sensors::V1_0::AdditionalInfo;
using ::android::hardware::sensors::V1_0::Event;
using ::android::hardware::sensors::V1_0::OperationMode;
using ::android::hardware::sensors::V1_0::Result;
using ::android::hardware::sensors::V1_0::SensorInfo;
using ::android::hardware::sensors::V1_0::SensorType;
using ::sensor::hal::configuration::V1_0::Configuration;

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace subhal {
namespace implementation {

// HWSensorBase represents the actual physical sensor provided as the IIO device
class PressureSensor : public HWSensorBase {
  public:
    PressureSensor(int32_t sensorHandle, ISensorsEventCallback* callback,
           struct iio_device_data& iio_data,
           const std::optional<std::vector<Configuration>>& config);
    ~PressureSensor();
    void run();
    void activate(bool enable);
    void setupSysfsTrigger(const std::string& device_dir, uint8_t dev_num, bool enable);
    void processScanData(char* data,Event* evt, int mChannelIndex);
  private:
    std::string mSysfspath;
};

}  // namespace implementation
}  // namespace subhal
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
#endif  // ANDROID_HARDWARE_SENSORS_V2_0_PRESSURESENSOR_H
