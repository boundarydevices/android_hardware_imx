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
#pragma once
#include "Sensor.h"

namespace nxp_sensors_subhal {

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
    void setupHrtimerTrigger(const std::string& device_dir, uint8_t dev_num, bool enable);
    void processScanData(char* data,Event* evt, int mChannelIndex);
    void setOperationMode(OperationMode mode);
    bool supportsDataInjection() const;
    Result injectEvent(const Event& event);

  private:
    std::string mSysfspath;
};

}  // namespace nxp_sensors_subhal
