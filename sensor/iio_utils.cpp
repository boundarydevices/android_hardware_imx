/*
 * Copyright (C) 2020 The Android Open Source Project
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
#define LOG_TAG "NXPIIOSensorSubHal"

#include "iio_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <log/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>

static const char* IIO_DEVICE_BASE = "iio:device";
static const char* IIO_SCAN_ELEMENTS_EN = "_en";
static const char* IIO_STEP_BUFFER_ENABLE = "in_steps_en";
static const char* IIO_SFA_FILENAME = "sampling_frequency_available";
static const char* IIO_SCALE_FILENAME = "_scale";
static const char* IIO_SAMPLING_FREQUENCY = "_sampling_frequency";
static const char* IIO_BUFFER_ENABLE = "buffer/enable";
static const char* IIO_NAME_FILENAME = "name";
static const char* IIO_MAX_RANGE_FILENAME = "sensor_max_range";
static const char* IIO_RESOLUTION_FILENAME = "sensor_resolution";
static const char* IIO_LIGHT_INPUT = "in_illuminance0_input";
static const char* IIO_STEPCOUNTER_INPUT = "events/in_steps_change_value";
static const char* IIO_TRIGGER = "/sys/devices/iio_sysfs_trigger/";
static const char* IIO_HRTIMER_TRIGGER = "/config/iio/triggers/hrtimer/";
static const char* IIO_CURRENT_TRIGGER = "/trigger/current_trigger";
static const char* IIO_DATA_TRIGGER = "/sys/bus/iio/devices/iio_sysfs_trigger/";

namespace android {
namespace hardware {
namespace sensors {
namespace V2_1 {
namespace subhal {
namespace implementation {

const char* DEFAULT_IIO_DIR = "/sys/bus/iio/devices/";

using DirPtr = std::unique_ptr<DIR, decltype(&closedir)>;
using FilePtr = std::unique_ptr<FILE, decltype(&fclose)>;

static bool str_has_prefix(const char* s, const char* prefix) {
    if (!s || !prefix) return false;

    const auto len_s = strlen(s);
    const auto len_prefix = strlen(prefix);
    if (len_s < len_prefix) return false;
    return std::equal(s, s + len_prefix, prefix);
}

static bool str_has_suffix(const char* s, const char* suffix) {
    if (!s || !suffix) return false;

    const auto len_s = strlen(s);
    const auto len_suffix = strlen(suffix);
    if (len_s < len_suffix) return false;
    return std::equal(s + len_s - len_suffix, s + len_s, suffix);
}

static int sysfs_opendir(const std::string& name, DirPtr* dp) {
    if (dp == nullptr) {
        return -EINVAL;
    }

    int dir_fd = openat(AT_FDCWD, name.c_str(), O_RDONLY | O_DIRECTORY);
    if (dir_fd == -1) {
        return -errno;
    }

    DIR* tmp = fdopendir(dir_fd);
    if (tmp == nullptr) {
        close(dir_fd);
        return -errno;
    }

    dp->reset(tmp);

    return 0;
}

// TODO(egranata): could this (and _read_ below), infer the fmt string directly
// from the type of value being passed in? that would be a safer alternative
template <typename T>
static int sysfs_write_val(const std::string& f, const std::string& fmt, const T value) {
    FilePtr fp = {fopen(f.c_str(), "r+"), fclose};
    if (nullptr == fp) return -errno;

    fprintf(fp.get(), fmt.c_str(), value);

    return 0;
}

static int sysfs_write_uint(const std::string& file, const unsigned int val) {
    return sysfs_write_val(file, "%u", val);
}

static int sysfs_write_double(const std::string& file, const double val) {
    return sysfs_write_val(file, "%f", val);
}

static int sysfs_write_str(const std::string& f, const std::string& fmt) {
    FilePtr fp = {fopen(f.c_str(), "r+"), fclose};
    if (nullptr == fp)
        return -errno;

    fprintf(fp.get(), "%s\n", fmt.c_str());

    return 0;
}

template <typename T>
static int sysfs_read_val(const std::string& f, const std::string& fmt, const T* value) {
    if (!value) return -EINVAL;

    FilePtr fp = {fopen(f.c_str(), "r"), fclose};
    if (nullptr == fp) return -errno;

    const int ret = fscanf(fp.get(), fmt.c_str(), value);
    return (ret == 1) ? 0 : -EINVAL;
}

static int sysfs_read_uint8(const std::string& file, uint8_t* val) {
    return sysfs_read_val(file, "%hhu\n", val);
}

static int sysfs_read_uint(const std::string& file, unsigned int* val) {
    return sysfs_read_val(file, "%u\n", val);
}

static int sysfs_read_float(const std::string& file, float* val) {
    return sysfs_read_val(file, "%f\n", val);
}

static int sysfs_read_int64(const std::string& file, int64_t* val) {
    return sysfs_read_val(file, "%lld\n", val);
}

static int sysfs_read_str(const std::string& file, std::string* str) {
    std::ifstream infile(file);
    if (!infile.is_open()) return -EINVAL;

    if (!std::getline(infile, *str))
        return -EINVAL;
    else
        return 0;
}

static int check_file(const std::string& filename) {
    struct stat info;
    return stat(filename.c_str(), &info);
}

int enable_sensor(const std::string& device_dir, const bool enable) {
    int err = check_file(device_dir);
    if (!err) {
        std::string enable_file = device_dir;
        enable_file += "/";
        enable_file += IIO_BUFFER_ENABLE;
        err = sysfs_write_uint(enable_file, enable);
    }

    return err;
}

int enable_step_sensor(const std::string& device_dir, const bool enable) {
    int err = check_file(device_dir);
    if (!err) {
        std::string enable_file = device_dir;
        enable_file += "/";
        enable_file += IIO_STEP_BUFFER_ENABLE;
        err = sysfs_write_uint(enable_file, enable);
    }
    return err;
}

int add_hrtimer_trigger(const std::string& device_dir, uint8_t dev_num, const bool enable) {
    int err = -1;

    std::string hrtimer_dir = IIO_HRTIMER_TRIGGER;
    std::string tri_value = "hrtimer_trigger";
    tri_value += std::to_string(dev_num);
    hrtimer_dir += tri_value;

    std::string current_trigger = device_dir;
    current_trigger += IIO_CURRENT_TRIGGER;

    if (enable) {
        int result = mkdir(hrtimer_dir.c_str(), 644);
        if (result == -1 && errno != EEXIST)
            goto failed;
        err = sysfs_write_str(current_trigger, tri_value);
    } else {
        err = sysfs_write_str(current_trigger, "");
    }

    if (err != 0)
        ALOGE("write current_trigger failed \n");

failed:
    return err;
}

int add_trigger(const std::string& device_dir, uint8_t dev_num, const bool enable) {
    int err;
    std::string enable_file = IIO_TRIGGER;
    std::string current_trigger = device_dir;
    std::string tri_value = "sysfstrig";
    if (enable)
        enable_file += "add_trigger";
    else
        enable_file += "remove_trigger";

    err = sysfs_write_uint(enable_file, dev_num);
    if (err != 0) {
        ALOGE("write enable_file failed \n");
        goto failed;
    }
    tri_value += std::to_string(dev_num);
    current_trigger += IIO_CURRENT_TRIGGER;

    err = sysfs_write_str(current_trigger, tri_value);
    if (err != 0)
        ALOGE("write current_trigger failed \n");

failed:
    return err;
}

int trigger_data(int dev_num, int64_t trigger_period_ns) {
    std::string scan_dir;
    std::string filename;
    DirPtr dp(nullptr, closedir);
    const struct dirent* ent;

    scan_dir = IIO_DATA_TRIGGER;
    int err = sysfs_opendir(scan_dir, &dp);
    if (err)
        return err;

    while (ent = readdir(dp.get()), ent != nullptr) {
        if (!str_has_prefix(ent->d_name, "trigger"))
            continue;

        std::string trigger_name = scan_dir;
        trigger_name += ent->d_name;
        trigger_name += "/name";

        FilePtr fp = {fopen(trigger_name.c_str(), "r"), fclose};
        if (fp == nullptr)
            continue;

        int index;
        const int ret = fscanf(fp.get(), "sysfstrig%d", &index);
        if (ret < 0)
            continue;

        if (index != dev_num)
            continue;

        std::string trigger_now = scan_dir;
        trigger_now += ent->d_name;
        trigger_now += "/trigger_now";
        usleep(trigger_period_ns / 1000);
        err = sysfs_write_uint(trigger_now, 1);
    }

    return 0;
}

static int get_sampling_frequency_available(const std::string& device_dir,
                                            std::vector<double>* sfa) {
    int ret = 0;
    char* rest;
    std::string line;
    DirPtr dp(nullptr, closedir);
    const struct dirent* ent;

    ret = sysfs_opendir(device_dir, &dp);
    if (ret) return ret;
    while (ent = readdir(dp.get()), ent != nullptr) {
        if (str_has_suffix(ent->d_name, IIO_SFA_FILENAME)) {
            std::string filename = device_dir;
            filename += "/";
            filename += ent->d_name;
            ret = sysfs_read_str(filename, &line);
            if (ret < 0) return ret;
            char* pch = strtok_r(const_cast<char*>(line.c_str()), " ,", &rest);
            while (pch != nullptr) {
                sfa->push_back(atof(pch));
                pch = strtok_r(nullptr, " ,", &rest);
            }
        }
    }

    return ret;
}

static int get_sensor_name(const std::string& device_dir, std::string* name) {
    const std::string filename = device_dir + "/" + IIO_NAME_FILENAME;

    return sysfs_read_str(filename, name);
}

int set_sampling_frequency(const std::string& device_dir, const double frequency) {
    DirPtr dp(nullptr, closedir);
    const struct dirent* ent;

    int ret = sysfs_opendir(device_dir, &dp);
    if (ret) return ret;
    while (ent = readdir(dp.get()), ent != nullptr) {
        if (str_has_suffix(ent->d_name, IIO_SAMPLING_FREQUENCY)) {
            std::string filename = device_dir;
            filename += "/";
            filename += ent->d_name;
            ret = sysfs_write_double(filename, frequency);
        }
    }
    return ret;
}

static int get_sensor_scale(const std::string& device_dir, float* scale, SensorType type) {
    DirPtr dp(nullptr, closedir);
    const struct dirent* ent;
    int err;
    bool support_scale = false;
    std::string filename;
    if (scale == nullptr) {
        return -EINVAL;
    }
    err = sysfs_opendir(device_dir, &dp);
    if (err) return err;
    while (ent = readdir(dp.get()), ent != nullptr) {
        if (str_has_suffix(ent->d_name, IIO_SCALE_FILENAME)) {
            filename = device_dir;
            filename += "/";
            filename += ent->d_name;
            if (((type != SensorType::AMBIENT_TEMPERATURE) && (type != SensorType::PRESSURE)) ||
                ((type == SensorType::AMBIENT_TEMPERATURE) && strstr(filename.c_str(), "temp")) ||
                ((type == SensorType::PRESSURE) && strstr(filename.c_str(), "pressure"))) {
                    err = sysfs_read_float(filename, scale);
                    support_scale = true;
                    break;
                }
        }
    }
    if (!support_scale)
        return -EINVAL;

    return err;
}

static int get_sensor_max_range(const std::string& device_dir, int64_t* max_range) {
    const std::string filename = device_dir + "/" + IIO_MAX_RANGE_FILENAME;

    return sysfs_read_int64(filename, max_range);
}

static int get_sensor_resolution(const std::string& device_dir, float* resolution) {
    const std::string filename = device_dir + "/" + IIO_RESOLUTION_FILENAME;

    return sysfs_read_float(filename, resolution);
}

int get_light_value(const std::string& device_dir, unsigned int* light) {
    const std::string filename = device_dir + "/" + IIO_LIGHT_INPUT;
    return sysfs_read_uint(filename, light);
}

int get_stepcounter_value(const std::string& device_dir, unsigned int* stepcounter) {
    const std::string filename = device_dir + "/" + IIO_STEPCOUNTER_INPUT;
    return sysfs_read_uint(filename, stepcounter);
}

int64_t get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_us = std::chrono::time_point_cast<std::chrono::microseconds>(now);
    return now_us.time_since_epoch().count();
}

static bool iterateSensorsInPath(const std::string& path,
                               const std::vector<sensors_supported_hal>& supported_sensors,
                                std::string* name, std::vector<sensors_supported_hal>* sensor) {
    std::string name_file = path + "/name";
    std::ifstream iio_file(name_file.c_str());
    if (!iio_file)
        return false;
    std::string iio_name;
    std::getline(iio_file, iio_name);
    for (auto& supported_sensor : supported_sensors) {
        if (supported_sensor.name == iio_name)
            sensor->push_back(supported_sensor);
    }
    *name = std::move(iio_name);
    return true;
}

int load_iio_devices(std::string iio_dir, std::vector<iio_device_data>* iio_data,
                     DeviceFilterFunction filter) {
    DirPtr dp(nullptr, closedir);
    const struct dirent* ent;
    int err;

    if (!iio_dir.empty() && iio_dir.back() != '/') iio_dir += '/';

    std::ifstream iio_file;
    const auto iio_base_len = strlen(IIO_DEVICE_BASE);
    err = sysfs_opendir(iio_dir, &dp);
    if (err) return err;
    while (ent = readdir(dp.get()), ent != nullptr) {
        if (!str_has_prefix(ent->d_name, IIO_DEVICE_BASE)) continue;

        std::string path_device = iio_dir;
        path_device += ent->d_name;
        std::vector<sensors_supported_hal> sensor_matches;
        std::string iio_name;
        if (!iterateSensorsInPath(path_device, supported_sensors, &iio_name, &sensor_matches)) continue;

        for (auto& sensor_match : sensor_matches) {
            iio_device_data iio_dev_data;
            iio_dev_data.sysfspath.append(path_device, 0, iio_dir.size() + strlen(ent->d_name));
            iio_dev_data.type = sensor_match.type;
            err = get_sensor_name(iio_dev_data.sysfspath, &iio_dev_data.name);
            if (err) {
                ALOGE("get_sensor_name for %s returned error %d", path_device.c_str(), err);
                continue;
            }

            ALOGI("found sensor %s at path %s", iio_dev_data.name.c_str(), path_device.c_str());
            err = get_sampling_frequency_available(iio_dev_data.sysfspath,
                                                &iio_dev_data.sampling_freq_avl);
            if (err < 0) {
                ALOGE("get_sampling_frequency_available for %s returned error %d", path_device.c_str(),
                    err);
                iio_dev_data.sampling_freq_avl[0] = 100;
            }

            std::sort(iio_dev_data.sampling_freq_avl.begin(), iio_dev_data.sampling_freq_avl.end());
            err = get_sensor_scale(iio_dev_data.sysfspath, &iio_dev_data.scale, iio_dev_data.type);
            if (err < 0) {
                iio_dev_data.scale = 0.015258f;
                ALOGI("get_sensor_scale for %s returned error %d", path_device.c_str(), err);
            }
            err = get_sensor_max_range(iio_dev_data.sysfspath, &iio_dev_data.max_range);
            if (err < 0) {
                iio_dev_data.max_range = 16000;
                ALOGI("get_sensor_max_range for %s returned error %d", path_device.c_str(), err);
            }
            err = get_sensor_resolution(iio_dev_data.sysfspath, &iio_dev_data.resolution);
            if (err < 0) {
                iio_dev_data.resolution = 1.0f;
                ALOGI("get_sensor_resolution for %s returned error %d", path_device.c_str(), err);
            }

            int n = sscanf(ent->d_name + iio_base_len, "%hhu", &iio_dev_data.iio_dev_num);
            if (n > 0)
                iio_data->push_back(iio_dev_data);
        }
    }
    return 0;
}

static int get_scan_type(const std::string& device_dir, struct iio_info_channel* chanInfo) {
    DirPtr dp(nullptr, closedir);
    const struct dirent* ent;
    std::string scan_dir;
    std::string filename;
    std::string type_name;
    char signchar, endianchar;
    unsigned int storage_bits;

    if (chanInfo == nullptr) {
        return -EINVAL;
    }
    scan_dir = device_dir;
    scan_dir += "/scan_elements";
    const int err = sysfs_opendir(scan_dir, &dp);
    if (err) return err;
    type_name = chanInfo->name;
    type_name += "_type";
    while (ent = readdir(dp.get()), ent != nullptr) {
        if (strcmp(ent->d_name, type_name.c_str()) == 0) {
            filename = scan_dir;
            filename += "/";
            filename += ent->d_name;
            FilePtr fp = {fopen(filename.c_str(), "r"), fclose};
            if (fp == nullptr) continue;
            const int ret = fscanf(fp.get(), "%ce:%c%hhu/%u>>%hhu", &endianchar, &signchar,
                                   &chanInfo->bits_used, &storage_bits, &chanInfo->shift);
            if (ret < 0) continue;
            chanInfo->big_endian = (endianchar == 'b');
            chanInfo->sign = (signchar == 's');
            chanInfo->storage_bytes = (storage_bits >> 3);
        }
    }
    return 0;
}

int scan_elements(const std::string& device_dir, struct iio_device_data* iio_data) {
    DirPtr dp(nullptr, closedir);
    const struct dirent* ent;
    std::string scan_dir;
    std::string filename;
    uint8_t temp;
    int ret;

    if (iio_data == nullptr) {
        return -EINVAL;
    }
    scan_dir = device_dir;
    scan_dir += "/scan_elements";
    ret = sysfs_opendir(scan_dir, &dp);
    if (ret) return ret;
    while (ent = readdir(dp.get()), ent != nullptr) {
        if (str_has_suffix(ent->d_name, IIO_SCAN_ELEMENTS_EN)) {
            filename = scan_dir;
            filename += "/";
            filename += ent->d_name;
            ret = sysfs_write_uint(filename, ENABLE_CHANNEL);
            if (ret == 0) {
                ret = sysfs_read_uint8(filename, &temp);
                if ((ret == 0) && (temp == 1)) {
                    iio_info_channel chan_info;
                    chan_info.name = strndup(ent->d_name,
                                             strlen(ent->d_name) - strlen(IIO_SCAN_ELEMENTS_EN));
                    filename = scan_dir;
                    filename += "/";
                    filename += chan_info.name;
                    filename += "_index";
                    ret = sysfs_read_uint8(filename, &chan_info.index);
                    if (ret) {
                        ALOGE("Getting index for channel %s for sensor %s returned error %d",
                              chan_info.name.c_str(), device_dir.c_str(), ret);
                        return ret;
                    }
                    ret = get_scan_type(device_dir, &chan_info);
                    if (ret) {
                        ALOGE("Getting scan type for channel %s sensor %s returned error %d",
                              chan_info.name.c_str(), device_dir.c_str(), ret);
                        return ret;
                    }
                    iio_data->channelInfo.push_back(chan_info);
                } else {
                    ALOGE("Not able to successfully enable channel %s for sensor %s error %d",
                          ent->d_name, device_dir.c_str(), ret);
                    return ret;
                }
            } else {
                ALOGE("Enabling scan channel %s for sensor %s returned error %d", ent->d_name,
                      device_dir.c_str(), ret);
                return ret;
            }
        }
    }
    return ret;
}
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_1
}  // namespace sensors
}  // namespace hardware
}  // namespace android
