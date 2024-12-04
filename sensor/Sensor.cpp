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

#include "Sensor.h"
#include <hardware/sensors.h>
#include <log/log.h>
#include <utils/SystemClock.h>
#include <cmath>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_1 {
namespace subhal {
namespace implementation {

using ::android::hardware::sensors::V1_0::AdditionalInfoType;
using ::android::hardware::sensors::V1_0::MetaDataEventType;
using ::android::hardware::sensors::V1_0::SensorFlagBits;
using ::android::hardware::sensors::V1_0::SensorStatus;
using ::sensor::hal::configuration::V1_0::Location;
using ::sensor::hal::configuration::V1_0::Orientation;

SensorBase::SensorBase(int32_t sensorHandle, ISensorsEventCallback* callback, SensorType type)
    : mIsEnabled(false),
      mSamplingPeriodNs(0),
      mCallback(callback),
      mMode(OperationMode::NORMAL),
      mSensorThread(this) {
    mSensorInfo.type = type;
    mSensorInfo.sensorHandle = sensorHandle;
    mSensorInfo.vendor = "nxp";
    mSensorInfo.version = 1;
    mSensorInfo.fifoReservedEventCount = 0;
    mSensorInfo.fifoMaxEventCount = 100;
    mSensorInfo.requiredPermission = "";

    switch (type) {
        case SensorType::ACCELEROMETER:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_ACCELEROMETER;
            mSensorInfo.flags = SensorFlagBits::DATA_INJECTION | SensorFlagBits::CONTINUOUS_MODE;
            break;
        case SensorType::MAGNETIC_FIELD:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_MAGNETIC_FIELD;
            mSensorInfo.flags = SensorFlagBits::DATA_INJECTION | SensorFlagBits::CONTINUOUS_MODE;
            break;
        case SensorType::GYROSCOPE:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_GYROSCOPE;
            mSensorInfo.flags = SensorFlagBits::DATA_INJECTION | SensorFlagBits::CONTINUOUS_MODE;
            break;
        case SensorType::PRESSURE:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_PRESSURE;
            mSensorInfo.flags |= SensorFlagBits::CONTINUOUS_MODE;
            break;
        case SensorType::AMBIENT_TEMPERATURE:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_AMBIENT_TEMPERATURE;
            mSensorInfo.flags |= SensorFlagBits::ON_CHANGE_MODE;
            break;
        case SensorType::LIGHT:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_LIGHT;
            mSensorInfo.flags |= SensorFlagBits::ON_CHANGE_MODE;
            break;
        case SensorType::STEP_COUNTER:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_STEP_COUNTER;
            mSensorInfo.flags |= SensorFlagBits::ON_CHANGE_MODE;
            break;
        default:
            ALOGE("unsupported sensor type %d", type);
            break;
    }

    mSensorThread.start();
}

SensorBase::~SensorBase() {
    mIsEnabled = false;
}

bool SensorBase::isEnabled() const {
    return mIsEnabled;
}

OperationMode SensorBase::getOperationMode() const {
    return mMode;
}

HWSensorBase::~HWSensorBase() {
    close(mPollFdIio.fd);
}

const SensorInfo& SensorBase::getSensorInfo() const {
    return mSensorInfo;
}

void HWSensorBase::batch(int32_t samplingPeriodNs) {
     if (mPollFdIio.fd < 0 || mSensorInfo.name == "mpl3115" || mSensorInfo.type == SensorType::STEP_COUNTER)
        return;
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
        mSensorThread.notifyAll();
    }
}

void HWSensorBase::sendAdditionalInfoReport() {
    std::vector<Event> events;

    for (const auto& frame : mAdditionalInfoFrames) {
        events.emplace_back(Event{
                .timestamp = android::elapsedRealtimeNano(),
                .sensorHandle = mSensorInfo.sensorHandle,
                .sensorType = SensorType::ADDITIONAL_INFO,
                .u.additional = frame,
        });
    }

    if (!events.empty()) {
        mCallback->postEvents(events, mCallback->createScopedWakelock(isWakeUpSensor()));
    }
}

void HWSensorBase::setupHrtimerTrigger(const std::string& device_dir, uint8_t dev_num,
                                        bool enable) {
    add_hrtimer_trigger(device_dir, dev_num, enable);
}

void HWSensorBase::setupSysfsTrigger(const std::string& device_dir, uint8_t dev_num, bool enable) {
    add_trigger(device_dir, dev_num, enable);
}

void HWSensorBase::activate(bool enable) {
    std::unique_lock<std::mutex> lock(mSensorThread.lock());
    if (mIsEnabled != enable) {
        mIsEnabled = enable;
        if (mPollFdIio.fd >= 0 && mIioData.type != SensorType::STEP_COUNTER)
            setupSysfsTrigger(mIioData.sysfspath, mIioData.iio_dev_num, enable);
        enable_sensor(mIioData.sysfspath, enable);
        if (mIioData.type == SensorType::STEP_COUNTER)
            enable_step_sensor(mIioData.sysfspath, enable);
        if (enable) sendAdditionalInfoReport();
        mSensorThread.notifyAll();
    }
}

Result SensorBase::flush() {
    // Only generate a flush complete event if the sensor is enabled and if the sensor is not a
    // one-shot sensor.
    if (!mIsEnabled || (mSensorInfo.flags & static_cast<uint32_t>(SensorFlagBits::ONE_SHOT_MODE))) {
        return Result::BAD_VALUE;
    }

    // Note: If a sensor supports batching, write all of the currently batched events for the sensor
    // to the Event FMQ prior to writing the flush complete event.
    Event ev;
    ev.timestamp = elapsedRealtimeNano(),
    ev.sensorHandle = mSensorInfo.sensorHandle;
    ev.sensorType = SensorType::META_DATA;
    ev.u.meta.what = MetaDataEventType::META_DATA_FLUSH_COMPLETE;
    std::vector<Event> evs{ev};
    mCallback->postEvents(evs, mCallback->createScopedWakelock(isWakeUpSensor()));
    return Result::OK;
}

Result HWSensorBase::flush() {
    if (mPollFdIio.fd < 0 || mSensorInfo.name == "mpl3115" || mSensorInfo.type == SensorType::STEP_COUNTER)
        return Result::INVALID_OPERATION;
    Result result = Result::OK;
    result = SensorBase::flush();
    if (result == Result::OK) sendAdditionalInfoReport();
    return result;
}

template <size_t N>
static float getChannelData(const std::array<float, N>& channelData, int64_t map, bool negate) {
    return negate ? -channelData[map] : channelData[map];
}

void HWSensorBase::readSysfsRawData(Event* evt) {
    evt->sensorHandle = mSensorInfo.sensorHandle;
    evt->sensorType = mSensorInfo.type;
    switch (mSensorInfo.type) {
        case SensorType::ACCELEROMETER: {
            char buf_acc_x[64], buf_acc_y[64], buf_acc_z[64];
            ssize_t bytes_acc = 1;
            bytes_acc *= read(fd_acc_x, buf_acc_x, sizeof(buf_acc_x));
            lseek(fd_acc_x, 0L, SEEK_SET);
            bytes_acc *= read(fd_acc_y, buf_acc_y, sizeof(buf_acc_y));
            lseek(fd_acc_y, 0L, SEEK_SET);
            bytes_acc *= read(fd_acc_z, buf_acc_z, sizeof(buf_acc_z));
            lseek(fd_acc_z, 0L, SEEK_SET);

            if (bytes_acc <= 0)
                ALOGI("Error reading accelerometer x-axis data");
            else {
                evt->u.vec3.x = atoi(buf_acc_x) * 0.00976;
                evt->u.vec3.y = atoi(buf_acc_y) * 0.00976;
                evt->u.vec3.z = atoi(buf_acc_z) * 0.00976;
            }
        } break;
        case SensorType::MAGNETIC_FIELD: {
            char buf_mag_x[64], buf_mag_y[64], buf_mag_z[64];
            ssize_t bytes_mag = 1;
            bytes_mag *= read(fd_mag_x, buf_mag_x, sizeof(buf_mag_x));
            lseek(fd_mag_x, 0L, SEEK_SET);
            bytes_mag *= read(fd_mag_y, buf_mag_y, sizeof(buf_mag_y));
            lseek(fd_mag_y, 0L, SEEK_SET);
            bytes_mag *= read(fd_mag_z, buf_mag_z, sizeof(buf_mag_z));
            lseek(fd_mag_z, 0L, SEEK_SET);

            if (bytes_mag <= 0)
                ALOGI("Error reading magnetic x-axis data");
            else {
                evt->u.vec3.x = atoi(buf_mag_x) * 0.001;
                evt->u.vec3.y = atoi(buf_mag_y) * 0.001;
                evt->u.vec3.z = atoi(buf_mag_z) * 0.001;
            }
        } break;
        case SensorType::LIGHT:
            unsigned int light;
            get_light_value(mIioData.sysfspath, &light);
            evt->u.scalar = light;
            break;
        case SensorType::STEP_COUNTER:
            unsigned int stepcounter;
            get_stepcounter_value(mIioData.sysfspath, &stepcounter);
            evt->u.stepCount = stepcounter;
            break;
        default:
            ALOGE("unsupported sensor type %d", mSensorInfo.type);
            break;
    }
    evt->timestamp = get_timestamp();
}

void HWSensorBase::processScanData(char* data, Event* evt) {
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
                    sign_mask = static_cast<int64_t>(1) << (mIioData.channelInfo[i].bits_used - 1);
                    value_mask = sign_mask - 1;
                    if (val & sign_mask)
                        channelData[chanIdx] = -((~val & value_mask) +
                                                 1); /* Negative value: return 2-complement */
                    else
                        channelData[chanIdx] = (int64_t)val; /* Positive value */
            }
        }
    }

    switch (mSensorInfo.type) {
        case SensorType::ACCELEROMETER:
        case SensorType::MAGNETIC_FIELD:
        case SensorType::GYROSCOPE:
            float scale_mag;
            if (mIioData.name == "fxas21002c")
                scale_mag = 1 / mIioData.scale;
            else
                scale_mag = mIioData.scale;
            evt->u.vec3.x = getChannelData(channelData, mXMap, false) * scale_mag;
            evt->u.vec3.y = getChannelData(channelData, mYMap, false) * scale_mag;
            evt->u.vec3.z = getChannelData(channelData, mZMap, false) * scale_mag;
            break;
        case SensorType::PRESSURE:
            // To meet CTS required srange, multiply pressure scale with 10.
            evt->u.scalar = getChannelData(channelData, 0, false) * mIioData.scale * 10;
            break;
        case SensorType::AMBIENT_TEMPERATURE:
            evt->u.scalar = getChannelData(channelData, 1, false) * mIioData.scale;
            break;
        default:
            ALOGE("unsupported sensor type %d", mSensorInfo.type);
            break;
    }

    // TODO:
    // when sensor driver fix sampling frequency/timestamp mismatch issue,
    // plan to switch to get timestamp from channel data.
    // evt->timestamp = getChannelData(channelData, mTMap, false) * 0.001;
    evt->timestamp = get_timestamp();
    evt->u.vec3.status = SensorStatus::ACCURACY_HIGH;
}

void HWSensorBase::pollForEvents() {
    if (mPollFdIio.fd < 0 || mIioData.type == SensorType::STEP_COUNTER) {
        Event evt;
        usleep(500000);
        readSysfsRawData(&evt);
        mCallback->postEvents({evt}, mCallback->createScopedWakelock(isWakeUpSensor()));
    } else {
        trigger_data(mIioData.iio_dev_num, mSamplingPeriodNs * 10);
        int err = poll(&mPollFdIio, 1, -1);
        if (err <= 0) {
            ALOGE("Sensor %s poll returned %d", mIioData.name.c_str(), err);
            return;
        }

        if (mPollFdIio.revents & POLLIN) {
            int read_size = read(mPollFdIio.fd, &mSensorRawData[0], mScanSize);
            if (read_size <= 0) {
                ALOGE("%s: Failed to read data from iio char device.", mIioData.name.c_str());
                return;
            }

            Event evt;
            processScanData(&mSensorRawData[0], &evt);
            mCallback->postEvents({evt}, mCallback->createScopedWakelock(isWakeUpSensor()));
        }
    }
}

void HWSensorBase::idleLoop() {
    mSensorThread.wait([this] {
        return ((mIsEnabled && mMode == OperationMode::NORMAL) || mSensorThread.isStopped());
    });
}

void HWSensorBase::pollSensor() {
    if (!mIsEnabled || mMode == OperationMode::DATA_INJECTION) {
        idleLoop();
    } else {
        pollForEvents();
    }
}

bool SensorBase::isWakeUpSensor() {
    return mSensorInfo.flags & static_cast<uint32_t>(SensorFlagBits::WAKE_UP);
}

void SensorBase::setOperationMode(OperationMode mode) {
    std::unique_lock<std::mutex> lock(mSensorThread.lock());
    if (mMode != mode) {
        mMode = mode;
        mSensorThread.notifyAll();
    }
}

bool SensorBase::supportsDataInjection() const {
    return mSensorInfo.flags & static_cast<uint32_t>(SensorFlagBits::DATA_INJECTION);
}

Result SensorBase::injectEvent(const Event& event) {
    Result result = Result::OK;
    if (event.sensorType == SensorType::ADDITIONAL_INFO) {
        // When in OperationMode::NORMAL, SensorType::ADDITIONAL_INFO is used to push operation
        // environment data into the device.
    } else if (!supportsDataInjection()) {
        result = Result::INVALID_OPERATION;
    } else if (mMode == OperationMode::DATA_INJECTION) {
        mCallback->postEvents({event}, mCallback->createScopedWakelock(isWakeUpSensor()));
    } else {
        result = Result::BAD_VALUE;
    }
    return result;
}

static status_t checkAxis(int64_t map) {
    if (map < 0 || map >= NUM_OF_DATA_CHANNELS)
        return BAD_VALUE;
    else
        return OK;
}

static std::optional<std::vector<Orientation>> getOrientation(
        std::optional<std::vector<Configuration>> config) {
    if (!config) return std::nullopt;
    if (config->empty()) return std::nullopt;
    Configuration& sensorCfg = (*config)[0];
    return sensorCfg.getOrientation();
}

static std::optional<std::vector<Location>> getLocation(
        std::optional<std::vector<Configuration>> config) {
    if (!config) return std::nullopt;
    if (config->empty()) return std::nullopt;
    Configuration& sensorCfg = (*config)[0];
    return sensorCfg.getLocation();
}

static status_t checkOrientation(std::optional<std::vector<Configuration>> config) {
    status_t ret = OK;
    std::optional<std::vector<Orientation>> sensorOrientationList = getOrientation(std::move(config));
    if (!sensorOrientationList) return OK;
    if (sensorOrientationList->empty()) return OK;
    Orientation& sensorOrientation = (*sensorOrientationList)[0];
    if (!sensorOrientation.getFirstX() || !sensorOrientation.getFirstY() ||
        !sensorOrientation.getFirstZ())
        return BAD_VALUE;

    int64_t xMap = sensorOrientation.getFirstX()->getMap();
    ret = checkAxis(xMap);
    if (ret != OK) return ret;
    int64_t yMap = sensorOrientation.getFirstY()->getMap();
    ret = checkAxis(yMap);
    if (ret != OK) return ret;
    int64_t zMap = sensorOrientation.getFirstZ()->getMap();
    ret = checkAxis(zMap);
    if (ret != OK) return ret;
    if (xMap == yMap || yMap == zMap || zMap == xMap) return BAD_VALUE;
    return ret;
}

void HWSensorBase::setAxisDefaultValues() {
    mXMap = 0;
    mYMap = 1;
    mZMap = 2;
    mXNegate = mYNegate = mZNegate = false;
}
void HWSensorBase::setOrientation(std::optional<std::vector<Configuration>> config) {
    std::optional<std::vector<Orientation>> sensorOrientationList = getOrientation(std::move(config));

    if (sensorOrientationList && !sensorOrientationList->empty()) {
        Orientation& sensorOrientation = (*sensorOrientationList)[0];

        if (sensorOrientation.getRotate()) {
            mXMap = sensorOrientation.getFirstX()->getMap();
            mXNegate = sensorOrientation.getFirstX()->getNegate();
            mYMap = sensorOrientation.getFirstY()->getMap();
            mYNegate = sensorOrientation.getFirstY()->getNegate();
            mZMap = sensorOrientation.getFirstZ()->getMap();
            mZNegate = sensorOrientation.getFirstZ()->getNegate();
        } else {
            setAxisDefaultValues();
        }
    } else {
        setAxisDefaultValues();
    }
}

static status_t checkIIOData(const struct iio_device_data& iio_data) {
    status_t ret = OK;
    for (auto i = 0u; i < iio_data.channelInfo.size(); i++) {
        if (iio_data.channelInfo[i].index > NUM_OF_DATA_CHANNELS) return BAD_VALUE;
    }
    return ret;
}

static status_t setSensorPlacementData(AdditionalInfo* sensorPlacement, int index, float value) {
    if (!sensorPlacement) return BAD_VALUE;

    int arraySize =
            sizeof(sensorPlacement->u.data_float) / sizeof(sensorPlacement->u.data_float[0]);
    if (index < 0 || index >= arraySize) return BAD_VALUE;

    sensorPlacement->u.data_float[index] = value;
    return OK;
}

status_t HWSensorBase::getSensorPlacement(AdditionalInfo* sensorPlacement,
                                          const std::optional<std::vector<Configuration>>& config) {
    if (!sensorPlacement) return BAD_VALUE;

    auto sensorLocationList = getLocation(config);
    if (!sensorLocationList) return BAD_VALUE;
    if (sensorLocationList->empty()) return BAD_VALUE;

    auto sensorOrientationList = getOrientation(config);
    if (!sensorOrientationList) return BAD_VALUE;
    if (sensorOrientationList->empty()) return BAD_VALUE;

    sensorPlacement->type = AdditionalInfoType::AINFO_SENSOR_PLACEMENT;
    sensorPlacement->serial = 0;
    memset(&sensorPlacement->u.data_float, 0, sizeof(sensorPlacement->u.data_float));

    Location& sensorLocation = (*sensorLocationList)[0];
    // SensorPlacementData is given as a 3x4 matrix consisting of a 3x3 rotation matrix (R)
    // concatenated with a 3x1 location vector (t) in row major order. Example: This raw buffer:
    // {x1,y1,z1,l1,x2,y2,z2,l2,x3,y3,z3,l3} corresponds to the following 3x4 matrix:
    //  x1 y1 z1 l1
    //  x2 y2 z2 l2
    //  x3 y3 z3 l3
    // LOCATION_X_IDX,LOCATION_Y_IDX,LOCATION_Z_IDX corresponds to the indexes of the location
    // vector (l1,l2,l3) in the raw buffer.
    status_t ret = setSensorPlacementData(sensorPlacement, HWSensorBase::LOCATION_X_IDX,
                                          sensorLocation.getX());
    if (ret != OK) return ret;
    ret = setSensorPlacementData(sensorPlacement, HWSensorBase::LOCATION_Y_IDX,
                                 sensorLocation.getY());
    if (ret != OK) return ret;
    ret = setSensorPlacementData(sensorPlacement, HWSensorBase::LOCATION_Z_IDX,
                                 sensorLocation.getZ());
    if (ret != OK) return ret;

    Orientation& sensorOrientation = (*sensorOrientationList)[0];
    if (sensorOrientation.getRotate()) {
        // If the HAL is already rotating the sensor orientation to align with the Android
        // Coordinate system, then the sensor rotation matrix will be an identity matrix
        // ROTATION_X_IDX, ROTATION_Y_IDX, ROTATION_Z_IDX corresponds to indexes of the
        // (x1,y1,z1) in the raw buffer.
        ret = setSensorPlacementData(sensorPlacement, HWSensorBase::ROTATION_X_IDX + 0, 1);
        if (ret != OK) return ret;
        ret = setSensorPlacementData(sensorPlacement, HWSensorBase::ROTATION_Y_IDX + 4, 1);
        if (ret != OK) return ret;
        ret = setSensorPlacementData(sensorPlacement, HWSensorBase::ROTATION_Z_IDX + 8, 1);
        if (ret != OK) return ret;
    } else {
        ret = setSensorPlacementData(
                sensorPlacement,
                HWSensorBase::ROTATION_X_IDX + 4 * sensorOrientation.getFirstX()->getMap(),
                sensorOrientation.getFirstX()->getNegate() ? -1 : 1);
        if (ret != OK) return ret;
        ret = setSensorPlacementData(
                sensorPlacement,
                HWSensorBase::ROTATION_Y_IDX + 4 * sensorOrientation.getFirstY()->getMap(),
                sensorOrientation.getFirstY()->getNegate() ? -1 : 1);
        if (ret != OK) return ret;
        ret = setSensorPlacementData(
                sensorPlacement,
                HWSensorBase::ROTATION_Z_IDX + 4 * sensorOrientation.getFirstZ()->getMap(),
                sensorOrientation.getFirstZ()->getNegate() ? -1 : 1);
        if (ret != OK) return ret;
    }
    return OK;
}

status_t HWSensorBase::setAdditionalInfoFrames(
        const std::optional<std::vector<Configuration>>& config) {
    AdditionalInfo additionalInfoSensorPlacement;
    status_t ret = getSensorPlacement(&additionalInfoSensorPlacement, config);
    if (ret != OK) return ret;

    const AdditionalInfo additionalInfoBegin = {
            .type = AdditionalInfoType::AINFO_BEGIN,
            .serial = 0,
    };
    const AdditionalInfo additionalInfoEnd = {
            .type = AdditionalInfoType::AINFO_END,
            .serial = 0,
    };

    mAdditionalInfoFrames.insert(
            mAdditionalInfoFrames.end(),
            {additionalInfoBegin, additionalInfoSensorPlacement, additionalInfoEnd});
    return OK;
}

HWSensorBase* HWSensorBase::buildSensor(int32_t sensorHandle, ISensorsEventCallback* callback,
                                        const struct iio_device_data& iio_data,
                                        const std::optional<std::vector<Configuration>>& config) {
    if (checkOrientation(config) != OK) {
        ALOGE("Orientation of the sensor %s in the configuration file is invalid",
              iio_data.name.c_str());
    }
    if (checkIIOData(iio_data) != OK) {
        ALOGE("IIO channel index of the sensor %s  is invalid", iio_data.name.c_str());
    }

    return new HWSensorBase(sensorHandle, callback, iio_data, config);
}

HWSensorBase::HWSensorBase(int32_t sensorHandle, ISensorsEventCallback* callback,
                           const struct iio_device_data& data,
                           const std::optional<std::vector<Configuration>>& config)
    : SensorBase(sensorHandle, callback, data.type) {
    std::string buffer_path;
    mSensorInfo.flags |= SensorFlagBits::CONTINUOUS_MODE;
    mSensorInfo.name = data.name;
    mSensorInfo.resolution = data.resolution * data.scale;
    mSensorInfo.maxRange = data.max_range * data.scale;
    mSensorInfo.power = 0;
    mIioData = data;
    setOrientation(config);
    status_t ret = setAdditionalInfoFrames(config);
    if (ret == OK) mSensorInfo.flags |= SensorFlagBits::ADDITIONAL_INFO;
    unsigned int max_sampling_frequency = 0;
    unsigned int min_sampling_frequency = UINT_MAX;
    for (auto i = 0u; i < data.sampling_freq_avl.size(); i++) {
        if (max_sampling_frequency < data.sampling_freq_avl[i])
            max_sampling_frequency = data.sampling_freq_avl[i];
        if (min_sampling_frequency > data.sampling_freq_avl[i])
            min_sampling_frequency = data.sampling_freq_avl[i];
    }
    if (max_sampling_frequency == 0)
        mSensorInfo.minDelay = 2500;
    else
        mSensorInfo.minDelay = frequency_to_us(max_sampling_frequency);

    mSensorInfo.maxDelay = frequency_to_us(min_sampling_frequency);
    mScanSize = 16;
    buffer_path = "/dev/iio:device";
    buffer_path.append(std::to_string(mIioData.iio_dev_num));
    mPollFdIio.fd = open(buffer_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (mPollFdIio.fd < 0 || mIioData.name == "mpl3115" ||
        mIioData.type == SensorType::STEP_COUNTER) {
        if (mIioData.type == SensorType::ACCELEROMETER) {
            static const char* IIO_ACC_X_RAW = "in_accel_x_raw";
            static const char* IIO_ACC_Y_RAW = "in_accel_y_raw";
            static const char* IIO_ACC_Z_RAW = "in_accel_z_raw";

            std::string x_filename = mIioData.sysfspath + "/" + IIO_ACC_X_RAW;
            std::string y_filename = mIioData.sysfspath + "/" + IIO_ACC_Y_RAW;
            std::string z_filename = mIioData.sysfspath + "/" + IIO_ACC_Z_RAW;

            fd_acc_x = unique_fd(open(x_filename.c_str(), O_RDONLY));
            fd_acc_y = unique_fd(open(y_filename.c_str(), O_RDONLY));
            fd_acc_z = unique_fd(open(z_filename.c_str(), O_RDONLY));
        } else if (mIioData.type == SensorType::MAGNETIC_FIELD) {
            static const char* IIO_MAG_X_RAW = "in_magn_x_raw";
            static const char* IIO_MAG_Y_RAW = "in_magn_y_raw";
            static const char* IIO_MAG_Z_RAW = "in_magn_z_raw";

            std::string x_filename = mIioData.sysfspath + "/" + IIO_MAG_X_RAW;
            std::string y_filename = mIioData.sysfspath + "/" + IIO_MAG_Y_RAW;
            std::string z_filename = mIioData.sysfspath + "/" + IIO_MAG_Z_RAW;

            fd_mag_x = unique_fd(open(x_filename.c_str(), O_RDONLY));
            fd_mag_y = unique_fd(open(y_filename.c_str(), O_RDONLY));
            fd_mag_z = unique_fd(open(z_filename.c_str(), O_RDONLY));
        }
        mSensorInfo.minDelay = 2500;
        mSensorInfo.maxDelay = 500000;
    }
    mPollFdIio.events = POLLIN;
    mPollFdIio.revents = 0;
    mSensorRawData.resize(mScanSize);
}

}  // namespace implementation
}  // namespace subhal
}  // namespace V2_1
}  // namespace sensors
}  // namespace hardware
}  // namespace android
