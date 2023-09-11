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
#include <android-base/unique_fd.h>

#include "Sensor.h"

using android::base::unique_fd;

namespace nxp_sensors_subhal {
// HWSensorBase represents the actual physical sensor provided as the IIO device
class AccMagSensor : public HWSensorBase {
public:
    AccMagSensor(int32_t sensorHandle, ISensorsEventCallback* callback,
                 struct iio_device_data& iio_data,
                 const std::optional<std::vector<Configuration>>& config);
    ~AccMagSensor();
    void batch(int32_t samplingPeriodNs);
    Result flush();
    void setOperationMode(OperationMode mode);
    bool isWakeUpSensor();
    void run();
    void activate(bool enable);
    void processScanData(Event* evt);
    bool supportsDataInjection() const;
    Result injectEvent(const Event& event);

private:
    std::string mSysfspath;
    std::string freq_file_name;
    unique_fd fd_acc_x;
    unique_fd fd_acc_y;
    unique_fd fd_acc_z;
    unique_fd fd_mag_x;
    unique_fd fd_mag_y;
    unique_fd fd_mag_z;
};

} // namespace nxp_sensors_subhal
