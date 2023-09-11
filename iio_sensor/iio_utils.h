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
#ifndef ANDROID_SENSORS_IIO_UTILS_H
#define ANDROID_SENSORS_IIO_UTILS_H

#include <android/hardware/sensors/2.1/types.h>
#include <dirent.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <stdint.h>
#include <sys/ioctl.h>

namespace nxp_sensors_subhal {

using ::android::hardware::sensors::V2_1::SensorType;

static constexpr auto DEFAULT_IIO_BUFFER_LEN = 2;
static constexpr auto DISABLE_CHANNEL = 0;
static constexpr auto ENABLE_CHANNEL = 1;

struct sensors_supported_hal {
    std::string name;
    SensorType type;
};

struct iio_info_channel {
    std::string name;
    uint8_t index;
    uint8_t storage_bytes;
    uint8_t bits_used;
    uint8_t shift;
    bool big_endian;
    bool sign;
};

struct iio_acc_mac_data {
    int x_raw;
    int y_raw;
    int z_raw;
};

struct iio_device_data {
    std::string name;
    std::string sysfspath;
    float resolution;
    float scale;
    SensorType type;
    std::vector<iio_info_channel> channelInfo;
    std::vector<double> sampling_freq_avl;
    std::vector<double> sampling_time_avl;
    uint8_t iio_dev_num;
    unsigned int power_microwatts;
    int64_t max_range;
};

int load_iio_devices(std::vector<iio_device_data>* iio_data,
                     const std::vector<sensors_supported_hal>& supported_sensors);
int scan_elements(const std::string& device_dir, struct iio_device_data* iio_data);
int enable_sensor(const std::string& name, const bool flag);
int enable_step_sensor(const std::string& name, const bool flag);
int set_sampling_frequency(const std::string& name, const double frequency);
int get_sampling_available(const std::string& time_file, std::vector<double>* sfa);
int get_sampling_time_available(const std::string& file, std::vector<double>* sfa);
int get_sampling_frequency_available(const std::string& file, std::vector<double>* sfa);
int get_sensor_light(const std::string& device_dir, unsigned int* light);
int get_sensor_stepcounter(const std::string& device_dir, unsigned int* stepcounter);
int add_trigger(const std::string& device_dir, uint8_t dev_num, const bool enable);
int add_hrtimer_trigger(const std::string& device_dir, uint8_t dev_num, const bool enable);
int trigger_data(int dev_num);
int64_t get_timestamp();
int get_pressure_scale(const std::string& file, float* scale);

} // namespace nxp_sensors_subhal
#endif
