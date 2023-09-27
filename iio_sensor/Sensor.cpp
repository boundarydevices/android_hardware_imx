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

#define LOG_TAG "android.hardware.sensors@2.1-nxp-IIO-Subhal"

#include "Sensor.h"
#ifdef CONFIG_LEGACY_SENSOR
#include "AccMagSensor.h"
#include "AnglvelSensor.h"
#include "LightSensor.h"
#include "PressureSensor.h"
#else
#include "StepCounterSensor.h"
#endif

namespace nxp_sensors_subhal {

using ::android::BAD_VALUE;
using ::android::OK;
using ::android::hardware::sensors::V1_0::AdditionalInfoType;
using ::sensor::hal::configuration::V1_0::Location;
using ::sensor::hal::configuration::V1_0::Orientation;

SensorBase::SensorBase(int32_t sensorHandle, ISensorsEventCallback* callback, SensorType type)
      : mIsEnabled(false), mSamplingPeriodNs(0), mCallback(callback), mMode(OperationMode::NORMAL) {
    mSensorInfo.type = type;
    mSensorInfo.sensorHandle = sensorHandle;
    mSensorInfo.vendor = "nxp";
    mSensorInfo.version = 1;
    mSensorInfo.fifoReservedEventCount = 0;
    mSensorInfo.fifoMaxEventCount = 100;
    mSensorInfo.requiredPermission = "";
    mSensorInfo.flags = SensorFlagBits::DATA_INJECTION | SensorFlagBits::CONTINUOUS_MODE;
    switch (type) {
        case SensorType::ACCELEROMETER:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_ACCELEROMETER;
            break;
        case SensorType::AMBIENT_TEMPERATURE:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_AMBIENT_TEMPERATURE;
            break;
        case SensorType::MAGNETIC_FIELD:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_MAGNETIC_FIELD;
            break;
        case SensorType::PRESSURE:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_PRESSURE;
            break;
        case SensorType::GYROSCOPE:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_GYROSCOPE;
            break;
        case SensorType::LIGHT:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_LIGHT;
            break;
        case SensorType::STEP_COUNTER:
            mSensorInfo.typeAsString = SENSOR_STRING_TYPE_STEP_COUNTER;
            break;
        default:
            ALOGE("unsupported sensor type %d", type);
            break;
    }
}

SensorBase::~SensorBase() {}

HWSensorBase::~HWSensorBase() {}

const SensorInfo& SensorBase::getSensorInfo() const {
    return mSensorInfo;
}

void HWSensorBase::batch(int32_t samplingPeriodNs) {
    samplingPeriodNs =
            std::clamp(samplingPeriodNs, mSensorInfo.minDelay * 1000, mSensorInfo.maxDelay * 1000);
    if (mSamplingPeriodNs != samplingPeriodNs) {
        // TODO: currently we still not support batch, disable it here.
        // unsigned int sampling_frequency = ns_to_frequency(samplingPeriodNs);
        // int i = 0;
        // mSamplingPeriodNs = samplingPeriodNs;
        // std::vector<double>::iterator low =
        //         std::lower_bound(mIioData.sampling_freq_avl.begin(),
        //                          mIioData.sampling_freq_avl.end(), sampling_frequency);
        // i = low - mIioData.sampling_freq_avl.begin();
        // set_sampling_frequency(mIioData.sysfspath, mIioData.sampling_freq_avl[i]);
        //  Wake up the 'run' thread to check if a new event should be generated now
        mWaitCV.notify_all();
    }
}

void HWSensorBase::sendAdditionalInfoReport() {
    std::vector<Event> events;

    for (const auto& frame : mAdditionalInfoFrames) {
        events.emplace_back(Event{
                .sensorHandle = mSensorInfo.sensorHandle,
                .sensorType = SensorType::ADDITIONAL_INFO,
                .timestamp = android::elapsedRealtimeNano(),
                .u.additional = frame,
        });
    }

    if (!events.empty())
        mCallback->postEvents(events, isWakeUpSensor());
}

void HWSensorBase::activate(bool enable) {
    std::unique_lock<std::mutex> lock(mRunMutex);
    if (mIsEnabled != enable) {
        mIsEnabled = enable;
        enable_sensor(mIioData.sysfspath, enable);
        mWaitCV.notify_all();
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
    ev.sensorHandle = mSensorInfo.sensorHandle;
    ev.sensorType = SensorType::META_DATA;
    ev.u.meta.what = MetaDataEventType::META_DATA_FLUSH_COMPLETE;
    std::vector<Event> evs{ev};
    mCallback->postEvents(evs, isWakeUpSensor());
    return Result::OK;
}

Result HWSensorBase::flush() {
    SensorBase::flush();
    return Result::OK;
}

template <size_t N>
static float getChannelData(const std::array<float, N>& channelData, int64_t map, bool negate) {
    return negate ? -channelData[map] : channelData[map];
}

void HWSensorBase::processScanData(uint8_t* data, Event* evt) {
    std::array<float, NUM_OF_DATA_CHANNELS> channelData;
    unsigned int chanIdx;
    evt->sensorHandle = mSensorInfo.sensorHandle;
    evt->sensorType = mSensorInfo.type;
    for (auto i = 0u; i < mIioData.channelInfo.size(); i++) {
        chanIdx = mIioData.channelInfo[i].index;

        const int64_t val =
                *reinterpret_cast<int64_t*>(data + chanIdx * mIioData.channelInfo[i].storage_bytes);
        // If the channel index is the last, it is timestamp
        // else it is sensor data
        if (chanIdx == mIioData.channelInfo.size() - 1) {
            evt->timestamp = val;
        } else {
            channelData[chanIdx] = static_cast<float>(val) * mIioData.scale;
        }
    }
}

bool SensorBase::isWakeUpSensor() {
    return mSensorInfo.flags & static_cast<uint32_t>(SensorFlagBits::WAKE_UP);
}

void SensorBase::setOperationMode(OperationMode mode) {
    std::unique_lock<std::mutex> lock(mRunMutex);
    if (mMode != mode) {
        mMode = mode;
        mWaitCV.notify_all();
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
        mCallback->postEvents(std::vector<Event>{event}, isWakeUpSensor());
    } else {
        result = Result::BAD_VALUE;
    }
    return result;
}

ssize_t HWSensorBase::calculateScanSize() {
    ssize_t numBytes = 0;
    for (auto i = 0u; i < mIioData.channelInfo.size(); i++) {
        numBytes += mIioData.channelInfo[i].storage_bytes;
    }
    return numBytes;
}

static status_t checkAxis(int64_t map) {
    if (map < 0 || map >= NUM_OF_DATA_CHANNELS)
        return BAD_VALUE;
    else
        return OK;
}

static std::optional<std::vector<Orientation>> getOrientation(
        std::optional<std::vector<Configuration>> config) {
    if (!config)
        return std::nullopt;
    if (config->empty())
        return std::nullopt;
    Configuration& sensorCfg = (*config)[0];
    return sensorCfg.getOrientation();
}

static std::optional<std::vector<Location>> getLocation(
        std::optional<std::vector<Configuration>> config) {
    if (!config)
        return std::nullopt;
    if (config->empty())
        return std::nullopt;
    Configuration& sensorCfg = (*config)[0];
    return sensorCfg.getLocation();
}

static status_t checkOrientation(std::optional<std::vector<Configuration>> config) {
    status_t ret = OK;
    std::optional<std::vector<Orientation>> sensorOrientationList = getOrientation(config);
    if (!sensorOrientationList)
        return OK;
    if (sensorOrientationList->empty())
        return OK;
    Orientation& sensorOrientation = (*sensorOrientationList)[0];
    if (!sensorOrientation.getFirstX() || !sensorOrientation.getFirstY() ||
        !sensorOrientation.getFirstZ())
        return BAD_VALUE;

    int64_t xMap = sensorOrientation.getFirstX()->getMap();
    ret = checkAxis(xMap);
    if (ret != OK)
        return ret;
    int64_t yMap = sensorOrientation.getFirstY()->getMap();
    ret = checkAxis(yMap);
    if (ret != OK)
        return ret;
    int64_t zMap = sensorOrientation.getFirstZ()->getMap();
    ret = checkAxis(zMap);
    if (ret != OK)
        return ret;
    if (xMap == yMap || yMap == zMap || zMap == xMap)
        return BAD_VALUE;
    return ret;
}

void HWSensorBase::setAxisDefaultValues() {
    mXMap = 0;
    mYMap = 1;
    mZMap = 2;
    mXNegate = mYNegate = mZNegate = false;
}

void HWSensorBase::setOrientation(std::optional<std::vector<Configuration>> config) {
    std::optional<std::vector<Orientation>> sensorOrientationList = getOrientation(config);

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
        if (iio_data.channelInfo[i].index > NUM_OF_DATA_CHANNELS)
            return BAD_VALUE;
    }
    return ret;
}

static status_t setSensorPlacementData(AdditionalInfo* sensorPlacement, int index, float value) {
    if (!sensorPlacement)
        return BAD_VALUE;

    int arraySize =
            sizeof(sensorPlacement->u.data_float) / sizeof(sensorPlacement->u.data_float[0]);
    if (index < 0 || index >= arraySize)
        return BAD_VALUE;

    sensorPlacement->u.data_float[index] = value;
    return OK;
}

status_t HWSensorBase::getSensorPlacement(AdditionalInfo* sensorPlacement,
                                          const std::optional<std::vector<Configuration>>& config) {
    if (!sensorPlacement)
        return BAD_VALUE;

    auto sensorLocationList = getLocation(config);
    if (!sensorLocationList)
        return BAD_VALUE;
    if (sensorLocationList->empty())
        return BAD_VALUE;

    auto sensorOrientationList = getOrientation(config);
    if (!sensorOrientationList)
        return BAD_VALUE;
    if (sensorOrientationList->empty())
        return BAD_VALUE;

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
    if (ret != OK)
        return ret;
    ret = setSensorPlacementData(sensorPlacement, HWSensorBase::LOCATION_Y_IDX,
                                 sensorLocation.getY());
    if (ret != OK)
        return ret;
    ret = setSensorPlacementData(sensorPlacement, HWSensorBase::LOCATION_Z_IDX,
                                 sensorLocation.getZ());
    if (ret != OK)
        return ret;

    Orientation& sensorOrientation = (*sensorOrientationList)[0];
    if (sensorOrientation.getRotate()) {
        // If the HAL is already rotating the sensor orientation to align with the Android
        // Coordinate system, then the sensor rotation matrix will be an identity matrix
        // ROTATION_X_IDX, ROTATION_Y_IDX, ROTATION_Z_IDX corresponds to indexes of the
        // (x1,y1,z1) in the raw buffer.
        ret = setSensorPlacementData(sensorPlacement, HWSensorBase::ROTATION_X_IDX + 0, 1);
        if (ret != OK)
            return ret;
        ret = setSensorPlacementData(sensorPlacement, HWSensorBase::ROTATION_Y_IDX + 4, 1);
        if (ret != OK)
            return ret;
        ret = setSensorPlacementData(sensorPlacement, HWSensorBase::ROTATION_Z_IDX + 8, 1);
        if (ret != OK)
            return ret;
    } else {
        ret = setSensorPlacementData(sensorPlacement,
                                     HWSensorBase::ROTATION_X_IDX +
                                             4 * sensorOrientation.getFirstX()->getMap(),
                                     sensorOrientation.getFirstX()->getNegate() ? -1 : 1);
        if (ret != OK)
            return ret;
        ret = setSensorPlacementData(sensorPlacement,
                                     HWSensorBase::ROTATION_Y_IDX +
                                             4 * sensorOrientation.getFirstY()->getMap(),
                                     sensorOrientation.getFirstY()->getNegate() ? -1 : 1);
        if (ret != OK)
            return ret;
        ret = setSensorPlacementData(sensorPlacement,
                                     HWSensorBase::ROTATION_Z_IDX +
                                             4 * sensorOrientation.getFirstZ()->getMap(),
                                     sensorOrientation.getFirstZ()->getNegate() ? -1 : 1);
        if (ret != OK)
            return ret;
    }
    return OK;
}

status_t HWSensorBase::setAdditionalInfoFrames(
        const std::optional<std::vector<Configuration>>& config) {
    AdditionalInfo additionalInfoSensorPlacement;
    status_t ret = getSensorPlacement(&additionalInfoSensorPlacement, config);
    if (ret != OK)
        return ret;

    const AdditionalInfo additionalInfoBegin = {
            .type = AdditionalInfoType::AINFO_BEGIN,
            .serial = 0,
    };
    const AdditionalInfo additionalInfoEnd = {
            .type = AdditionalInfoType::AINFO_END,
            .serial = 0,
    };

    mAdditionalInfoFrames.insert(mAdditionalInfoFrames.end(),
                                 {additionalInfoBegin, additionalInfoSensorPlacement,
                                  additionalInfoEnd});
    return OK;
}

HWSensorBase* HWSensorBase::buildSensor(int32_t sensorHandle, ISensorsEventCallback* callback,
                                        struct iio_device_data& iio_data,
                                        const std::optional<std::vector<Configuration>>& config) {
    if (checkOrientation(config) != OK) {
        ALOGE("Orientation of the sensor %s in the configuration file is invalid",
              iio_data.name.c_str());
        return nullptr;
    }
    if (checkIIOData(iio_data) != OK) {
        ALOGE("IIO channel index of the sensor %s  is invalid", iio_data.name.c_str());
        return nullptr;
    }

#ifdef CONFIG_LEGACY_SENSOR
    if (iio_data.type == SensorType::LIGHT)
        return new LightSensor(sensorHandle, callback, iio_data, config);
    else if (iio_data.type == SensorType::PRESSURE)
        return new PressureSensor(sensorHandle, callback, iio_data, config);
    else if (iio_data.type == SensorType::AMBIENT_TEMPERATURE)
        return new PressureSensor(sensorHandle, callback, iio_data, config);
    else if (iio_data.type == SensorType::ACCELEROMETER)
        return new AccMagSensor(sensorHandle, callback, iio_data, config);
    else if (iio_data.type == SensorType::MAGNETIC_FIELD)
        return new AccMagSensor(sensorHandle, callback, iio_data, config);
    else if (iio_data.type == SensorType::GYROSCOPE)
        return new AnglvelSensor(sensorHandle, callback, iio_data, config);
#endif
#ifdef CONFIG_SENSOR_PEDOMETER
    if (iio_data.type == SensorType::STEP_COUNTER)
        return new StepCounterSensor(sensorHandle, callback, iio_data, config);
#endif
    return nullptr;
}

HWSensorBase::HWSensorBase(int32_t sensorHandle, ISensorsEventCallback* callback,
                           const struct iio_device_data& data,
                           const std::optional<std::vector<Configuration>>& config)
      : SensorBase(sensorHandle, callback, data.type) {
    mSensorInfo.name = data.name;
    mSensorInfo.resolution = data.resolution;
    mSensorInfo.maxRange = data.max_range * data.scale;
    mSensorInfo.power =
            (data.power_microwatts / 1000.f) / SENSOR_VOLTAGE_DEFAULT; // converting uW to mA
    mIioData = data;
    setOrientation(config);
    status_t ret = setAdditionalInfoFrames(config);
    if (ret == OK)
        mSensorInfo.flags |= SensorFlagBits::ADDITIONAL_INFO;
    unsigned int max_sampling_frequency = 0;
    unsigned int min_sampling_frequency = UINT_MAX;
    for (auto i = 0u; i < data.sampling_freq_avl.size(); i++) {
        if (max_sampling_frequency < data.sampling_freq_avl[i])
            max_sampling_frequency = data.sampling_freq_avl[i];
        if (min_sampling_frequency > data.sampling_freq_avl[i])
            min_sampling_frequency = data.sampling_freq_avl[i];
    }
    mSensorInfo.minDelay = frequency_to_us(max_sampling_frequency);
    mSensorInfo.maxDelay = frequency_to_us(min_sampling_frequency);
    mScanSize = calculateScanSize();
    mPollFdIio.events = POLLIN;
    mPollFdIio.revents = 0;
    mSensorRawData.resize(mScanSize);
}

} // namespace nxp_sensors_subhal
