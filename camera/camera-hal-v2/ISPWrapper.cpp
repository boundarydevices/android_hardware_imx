/*
 *  Copyright 2024 NXP.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
#define LOG_TAG "ISPWrapper"

#include "ISPWrapper.h"

#include <errno.h>
#include <linux/videodev2.h>
#include <log/log.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utils/Errors.h>

#include "VendorTags.h"
#include "hal_camera_metadata.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

namespace android {

ISPWrapper::ISPWrapper() {
    // Set ISP feature to it's default value.
    m_awb_mode = ANDROID_CONTROL_AWB_MODE_AUTO;
    m_ae_mode = ANDROID_CONTROL_AE_MODE_ON;

    m_exposure_time = 0;
    m_exposure_gain = 0;
    m_last_exposure_gain = -1.0;
    m_last_exposure_time = 0;

    m_ec_gain_min = 1.0;
    m_ec_gain_max = 15.5;
    m_brightness_min = -1.0;
    m_brightness_max = 1.0;
}

ISPWrapper::~ISPWrapper() {}

// Keep same sequence as camera_metadata_enum_android_control_awb_mode_t defined in
// camera_metadata_tags.h
#define WB_MODE_NUM 9
static WbGains wb_gains_list[WB_MODE_NUM] = {
        {1.0, 1.0}, // ANDROID_CONTROL_AWB_MODE_OFF, don't care the value, just match android tag.
        {1.0, 1.0}, // ANDROID_CONTROL_AWB_MODE_AUTO, don't care the value, just match android tag.
        {1.09915, 3.1024}, // ANDROID_CONTROL_AWB_MODE_INCANDESCENT
        {1.58448, 2.5385}, // ANDROID_CONTROL_AWB_MODE_FLUORESCENT
        {1.28448, 2.1385}, // ANDROID_CONTROL_AWB_MODE_WARM_FLUORESCENT
        {1.66425, 1.9972}, // ANDROID_CONTROL_AWB_MODE_DAYLIGHT
        {1.94499, 1.6718}, // ANDROID_CONTROL_AWB_MODE_CLOUDY_DAYLIGHT
        {1.36191, 2.4337}, // ANDROID_CONTROL_AWB_MODE_TWILIGHT
        {1.36191, 2.4337}  // ANDROID_CONTROL_AWB_MODE_SHADE
};

int ISPWrapper::enableAWB(bool enable, libcamera::ControlList &controls) {
    controls.set(libcamera::controls::AwbEnable, enable);

    return 0;
}

int ISPWrapper::processAWB(uint8_t mode, libcamera::ControlList &controls, bool force) {
    int ret = 0;

    ALOGV("%s, mode %d, force %d", __func__, mode, force);

    if ((mode == m_awb_mode) && (force == false))
        return 0;

    ALOGI("%s, change WB mode from %d to %d, force %d", __func__, m_awb_mode, mode, force);

    if ((mode == ANDROID_CONTROL_AWB_MODE_AUTO) || (mode == ANDROID_CONTROL_AWB_MODE_OFF)) {
        bool bEnable = (mode == ANDROID_CONTROL_AWB_MODE_AUTO) ? true : false;
        ret = enableAWB(bEnable, controls);
        if (ret == 0)
            m_awb_mode = mode;

        return ret;
    }

    // If shift from AWB to MWB, first disable AWB.
    if (m_awb_mode == ANDROID_CONTROL_AWB_MODE_AUTO) {
        ret = enableAWB(false, controls);
        if (ret) {
            return ret;
        }
    }

    WbGains gains = wb_gains_list[mode];
    static float fColourGains[] = {0.0, 0.0};
    fColourGains[0] = gains.Red;
    fColourGains[1] = gains.Blue;

    controls.set(libcamera::controls::ColourGains, fColourGains);
    m_awb_mode = mode;

    return ret;
}

int ISPWrapper::processAeMode(uint8_t mode, libcamera::ControlList &controls, bool force) {
    if ((mode != ANDROID_CONTROL_AE_MODE_OFF) && (mode != ANDROID_CONTROL_AE_MODE_ON)) {
        ALOGW("%s: unsupported ae mode %d", __func__, mode);
        return BAD_VALUE;
    }

    if ((mode == m_ae_mode) && (force == false))
        return 0;

    ALOGI("%s: set ae mode to %d, force %d", __func__, mode, force);

    bool enable = (mode == ANDROID_CONTROL_AE_MODE_ON) ? true : false;

    controls.set(libcamera::controls::AeEnable, enable);
    m_ae_mode = mode;

    if (m_ae_mode == ANDROID_CONTROL_AE_MODE_ON) {
        m_exposure_time = 0;
        m_exposure_gain = 0;
    }

    return 0;
}

#define GAIN_LEVEL_MIN 1
#define GAIN_LEVEL_MAX 10
int ISPWrapper::processExposureGain(int32_t gain, libcamera::ControlList &controls, bool force) {
    int ret;

    if ((m_exposure_gain == gain) && (force == false))
        return 0;

    if (gain > GAIN_LEVEL_MAX)
        gain = GAIN_LEVEL_MAX;
    if (gain < GAIN_LEVEL_MIN)
        gain = GAIN_LEVEL_MIN;

    // first disable aec
    processAeMode(ANDROID_CONTROL_AE_MODE_OFF, controls);

    // calc the value to set
    float exposure_gain = m_ec_gain_min +
            ((gain - GAIN_LEVEL_MIN) * (m_ec_gain_max - m_ec_gain_min)) /
                    (GAIN_LEVEL_MAX - GAIN_LEVEL_MIN);

    ALOGI("%s: change gain from %d to %d, now exposure gain is %f, exposure time is %ld us, force %d",
          __func__, m_exposure_gain, gain, exposure_gain, m_last_exposure_time, force);

    controls.set(libcamera::controls::AnalogueGain, exposure_gain);
    m_last_exposure_gain = exposure_gain;
    m_exposure_gain = gain;

    return 0;
}

#define EXPOSURE_TIME_NS_MIN 116000
#define EXPOSURE_TIME_NS_MAX 33216000
#define NS_PER_US 1000ULL
int ISPWrapper::processExposureTime(int64_t exposureNs, libcamera::ControlList &controls,
                                    bool force) {
    if ((m_exposure_time == exposureNs) && (force == false))
        return 0;

    if (exposureNs > EXPOSURE_TIME_NS_MAX)
        exposureNs = EXPOSURE_TIME_NS_MAX;
    if (exposureNs < EXPOSURE_TIME_NS_MIN)
        exposureNs = EXPOSURE_TIME_NS_MIN;

    // first disable aec
    processAeMode(ANDROID_CONTROL_AE_MODE_OFF, controls);

    int64_t exposure_time_us = exposureNs / NS_PER_US;

    ALOGI("%s: change exposureNs from %ld to %ld, now exposure gain is %f, exposure time is %ld us, force %d",
          __func__, m_exposure_time, exposureNs, m_last_exposure_gain, exposure_time_us, force);

    controls.set(libcamera::controls::ExposureTime, exposure_time_us);
    m_last_exposure_time = exposure_time_us;
    m_exposure_time = exposureNs;

    return 0;
}

// Current tactic: don't return if some meta process failed,
// since may have other meta to process.
int ISPWrapper::process(HalCameraMetadata *pMeta, libcamera::ControlList &controls) {
    if (pMeta == NULL) {
        return BAD_VALUE;
    }

    status_t ret;
    camera_metadata_ro_entry entry;

    ret = pMeta->Get(ANDROID_CONTROL_AWB_MODE, &entry);
    processAWB(entry.data.u8[0], controls);

    ret = pMeta->Get(ANDROID_CONTROL_AE_MODE, &entry);
    if (ret == 0)
        processAeMode(entry.data.u8[0], controls);

    ret = pMeta->Get(VSI_EXPOSURE_GAIN, &entry);
    if (ret == 0)
        processExposureGain(entry.data.i32[0], controls);

    ret = pMeta->Get(ANDROID_SENSOR_EXPOSURE_TIME, &entry);
    if (ret == 0)
        processExposureTime(entry.data.i64[0], controls);

    return 0;
}

} // namespace android
