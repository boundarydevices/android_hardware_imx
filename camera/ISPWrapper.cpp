/*
 *  Copyright 2020 NXP.
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

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <log/log.h>
#include <utils/Errors.h>
#include "hal_camera_metadata.h"
#include "CameraConfigurationParser.h"
#include "ISPWrapper.h"
#include "VendorTags.h"

#define VIV_CTRL_NAME "viv_ext_ctrl"
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

namespace android {

ISPWrapper::ISPWrapper(CameraSensorMetadata *pSensorData, void *stream)
{
    m_fd = -1;
    m_ctrl_id = 0;
    m_SensorData = pSensorData;
    m_stream = stream;

    // Set ISP feature to it's default value
    m_awb_mode = ANDROID_CONTROL_AWB_MODE_AUTO;
    m_ae_mode = ANDROID_CONTROL_AE_MODE_ON;

    // When init, aec is on. m_exposure_comp is only valid when aec is off, set to an invalid value.
    m_exposure_comp = m_SensorData->mAeCompMax + 1;
    m_exposure_time = 0.0;
    m_exposure_gain = 0;

    memset(&m_dwePara, 0, sizeof(m_dwePara));
    // Align with daA3840_30mc_1080P.json
    m_dwePara.mode = DEWARP_MODEL_LENS_DISTORTION_CORRECTION;
    m_dwe_on = true;

    m_ec_gain_min = EXP_GAIN_MIN_DFT;
    m_ec_gain_max = EXP_GAIN_MAX_DFT;
    mLSCEnable = false;
    m_gamma = 0.0;

    m_brightness =  BRIGHTNESS_MAX + 1;
    m_contrast = CONTRAST_MAX + 1;
    m_saturation = SATURATION_MAX + 1;
    m_hue = HUE_MAX + 1;

    m_sharp_level = SHARP_LEVEL_MAX +1;

    m_last_exposure_gain = -1.0;
    m_last_exposure_time = -1.0;

    m_last_wb_r = -1.0;
    m_last_wb_gr = -1.0;
    m_last_wb_gb = -1.0;
    m_last_wb_b = -1.0;
}

ISPWrapper::~ISPWrapper()
{
}

int ISPWrapper::init(int fd)
{
    m_fd = fd;

    // already inited
    if(m_ctrl_id > 0)
        return 0;

    // get viv ctrl id by it's name "viv_ext_ctrl"
    struct v4l2_queryctrl queryctrl;
    memset(&queryctrl, 0, sizeof(queryctrl));

    queryctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;
    while (0 == ioctl(m_fd, VIDIOC_QUERYCTRL, &queryctrl)) {
        if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED)
            continue;

        ALOGI("%s Control %s", __func__, queryctrl.name);
        if (strcmp((char *)queryctrl.name, VIV_CTRL_NAME) == 0) {
            m_ctrl_id = queryctrl.id;
            ALOGI("%s, find viv ctrl id 0x%x", __func__, m_ctrl_id);
            break;
        }

        queryctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }

    return (m_ctrl_id > 0) ? 0 : NO_INIT;
}

int ISPWrapper::setFeature(const char *value)
{
    int ret = 0;
    struct v4l2_ext_controls ctrls;
    struct v4l2_ext_control ctrl;

    if(value == NULL)
        return BAD_VALUE;

    if ((m_fd <= 0) || (m_ctrl_id == 0))
        return NO_INIT;

    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = m_ctrl_id;
    ctrl.size = strlen (value) + 1;
    ctrl.string = strdup(value);

    memset(&ctrls, 0, sizeof(ctrls));
    ctrls.which = V4L2_CTRL_ID2WHICH(ctrl.id);
    ctrls.count = 1;
    ctrls.controls = &ctrl;

    ALOGI("setFeature, fd %d, id 0x%x, str %s", m_fd, m_ctrl_id, value);

    ret = ioctl(m_fd, VIDIOC_S_EXT_CTRLS, &ctrls);
    ALOGI("setFeature, ret %d", ret);
    if(ret < 0)
        ALOGE("%s VIDIOC_S_EXT_CTRLS failed, value %s, errno %d, %s",
            __func__, value, errno, strerror(errno));

    free(ctrl.string);

    return ret;
}

// Keep same sequence as camera_metadata_enum_android_control_awb_mode_t defined in camera_metadata_tags.h
#define WB_MODE_NUM 9
static WbGains wb_gains_list[WB_MODE_NUM] = {
    {1.0, 1.0, 1.0, 1.0}, // ANDROID_CONTROL_AWB_MODE_OFF, don't care the value, just match android tag.
    {1.0, 1.0, 1.0, 1.0}, // ANDROID_CONTROL_AWB_MODE_AUTO, don't care the value, just match android tag.
    {1.09915, 1.0, 1.0, 3.1024}, // ANDROID_CONTROL_AWB_MODE_INCANDESCENT
    {1.58448, 1.0, 1.0, 2.5385}, // ANDROID_CONTROL_AWB_MODE_FLUORESCENT
    {1.28448, 1.2, 1.2, 2.1385}, // ANDROID_CONTROL_AWB_MODE_WARM_FLUORESCENT
    {1.66425, 1.0, 1.0, 1.9972}, // ANDROID_CONTROL_AWB_MODE_DAYLIGHT
    {1.94499, 1.0, 1.0, 1.6718}, // ANDROID_CONTROL_AWB_MODE_CLOUDY_DAYLIGHT
    {1.36191, 1.0, 1.0, 2.4337}, // ANDROID_CONTROL_AWB_MODE_TWILIGHT
    {1.36191, 1.0, 1.0, 2.4337}  // ANDROID_CONTROL_AWB_MODE_SHADE
};

int ISPWrapper::enableAWB(bool enable)
{
    Json::Value jRequest, jResponse;

    jRequest[AWB_ENABLE_PARAMS] = enable;
    int ret = viv_private_ioctl(IF_AWB_S_EN, jRequest, jResponse);
    if(ret) {
        ALOGE("%s, enable %d, viv_private_ioctl failed, ret %d", __func__, enable, ret);
        return BAD_VALUE;
    }

    return 0;
}

int ISPWrapper::processAWB(uint8_t mode, bool force)
{
    int ret = 0;

    ALOGV("%s, mode %d, force %d", __func__, mode, force);

    if(mode >= WB_MODE_NUM) {
        ALOGW("%s, unsupported awb mode %d", __func__, mode);
        return BAD_VALUE;
    }

    ImxStream *imxStream = (ImxStream *)m_stream;
    if (imxStream->isPictureIntent() && (mode != ANDROID_CONTROL_AWB_MODE_OFF)) {
        ALOGI("%s: taking picture only support awb off", __func__);
        return 0;
    }

    if((mode == m_awb_mode) && (force == false))
        return 0;

    ALOGI("%s, change WB mode from %d to %d, force %d", __func__, m_awb_mode, mode, force);

    if ((mode == ANDROID_CONTROL_AWB_MODE_AUTO) || (mode == ANDROID_CONTROL_AWB_MODE_OFF)) {
        bool bEnable = (mode == ANDROID_CONTROL_AWB_MODE_AUTO) ? true : false;
        ret = enableAWB(bEnable);
        if (ret == 0)
            m_awb_mode = mode;

        return ret;
    }

    // If shift from AWB to MWB, first disable AWB.
    if (m_awb_mode == ANDROID_CONTROL_AWB_MODE_AUTO) {
        ret = enableAWB(false);
        if (ret)
            return ret;
    }

    WbGains gains = wb_gains_list[mode];
    Json::Value jRequest, jResponse;

    jRequest[WB_RED_PARAMS]     = gains.Red;
    jRequest[WB_GREEN_R_PARAMS] = gains.GreenR;
    jRequest[WB_GREEN_B_PARAMS] = gains.GreenB;
    jRequest[WB_BLUE_PARAMS]    = gains.Blue;

    ret = viv_private_ioctl(IF_WB_S_GAIN, jRequest, jResponse);
    if(ret) {
        ALOGE("%s, set wb mode %d failed, IF_WB_S_GAIN ret %d", __func__, mode, ret);
        return BAD_VALUE;
    }

    m_awb_mode = mode;

    return 0;
}

#define DWE_ON (char *)"{<id>:<pipeline.s.dwe.onoff>;<enable>: true}"
#define DWE_OFF (char *)"{<id>:<pipeline.s.dwe.onoff>;<enable>: false}"

int ISPWrapper::EnableDWE(bool on)
{
    if(on == m_dwe_on)
        return 0;

    char *str = on ? DWE_ON : DWE_OFF;
    int ret = setFeature(str);
    if(ret == 0)
        m_dwe_on = on;

    return ret;
}

// Current tactic: don't return if some meta process failed,
// since may have other meta to process.
int ISPWrapper::process(HalCameraMetadata *pMeta, uint32_t format)
{
    // Capture raw data need fist dwe off.
    if(format == HAL_PIXEL_FORMAT_RAW16) {
        return EnableDWE(false);
    }

    if(pMeta == NULL)
        return BAD_VALUE;

    // If not raw data, recover to the init state, dew on.
    EnableDWE(true);

    status_t ret;
    camera_metadata_ro_entry entry;

    ret = pMeta->Get(ANDROID_CONTROL_AWB_MODE, &entry);
    if(ret == 0)
        processAWB(entry.data.u8[0]);

// ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION is a para related to AEC.
// Not the exposure gain of VSI ISP lib, it needs disable AEC.
#if 0
    ret = pMeta->Get(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &entry);
    if(ret == 0)
        processExposureGain(entry.data.i32[0]);
#endif

    ret = pMeta->Get(ANDROID_CONTROL_AE_MODE, &entry);
    if(ret == 0)
        processAeMode(entry.data.u8[0]);

    ret = pMeta->Get(VSI_EXPOSURE_GAIN, &entry);
    if(ret == 0)
        processExposureGain(entry.data.i32[0]);

    ret = pMeta->Get(ANDROID_SENSOR_EXPOSURE_TIME, &entry);
    if(ret == 0)
        processExposureTime(entry.data.i64[0]);

    ret = pMeta->Get(VSI_DEWARP, &entry);
    if(ret == 0)
        processDewarp(entry.data.i32[0]);

    ret = pMeta->Get(VSI_HFLIP, &entry);
    if(ret == 0)
        processHFlip(entry.data.i32[0]);

    ret = pMeta->Get(VSI_VFLIP, &entry);
    if(ret == 0)
        processVFlip(entry.data.i32[0]);

    ret = pMeta->Get(VSI_LSC, &entry);
    if(ret == 0)
        processLSC(entry.data.i32[0]);

    ret = pMeta->Get(ANDROID_TONEMAP_GAMMA, &entry);
    if(ret == 0)
        processGamma(entry.data.f[0]);

    ret = pMeta->Get(VSI_BRIGHTNESS, &entry);
    if(ret == 0)
        processBrightness(entry.data.i32[0]);

    ret = pMeta->Get(VSI_CONTRAST, &entry);
    if(ret == 0)
        processContrast(entry.data.f[0]);

    ret = pMeta->Get(VSI_SATURATION, &entry);
    if(ret == 0)
        processSaturation(entry.data.f[0]);

    ret = pMeta->Get(VSI_HUE, &entry);
    if(ret == 0)
        processHue(entry.data.i32[0]);

    ret = pMeta->Get(VSI_SHARP_LEVEL, &entry);
    if(ret == 0)
        processSharpLevel(entry.data.u8[0]);

    return 0;
}


#define IF_EC_S_CFG       "ec.s.cfg"
#define IF_EC_G_CFG       "ec.g.cfg"
#define EC_GAIN_PARAMS    "gain"
#define EC_GAIN_MIN_PARAMS  "gain.min"
#define EC_GAIN_MAX_PARAMS  "gain.max"
#define EC_TIME_PARAMS    "time"

#define VIV_CUSTOM_CID_BASE   (V4L2_CID_USER_BASE | 0xf000)
#define V4L2_CID_VIV_EXTCTRL  (VIV_CUSTOM_CID_BASE + 1)
#define VIV_JSON_BUFFER_SIZE  (64*1024)

int ISPWrapper::viv_private_ioctl(const char *cmd, Json::Value& jsonRequest, Json::Value& jsonResponse)
{
    int ret = 0;

    if (!cmd) {
        ALOGE("cmd should anot be null!");
        return -1;
    }
    jsonRequest["id"] = cmd;
    jsonRequest["streamid"] = 0;

    struct v4l2_ext_controls ecs;
    struct v4l2_ext_control ec;
    Json::Reader reader;
    memset(&ecs, 0, sizeof(ecs));
    memset(&ec, 0, sizeof(ec));
    ec.string = new char[VIV_JSON_BUFFER_SIZE];
    ec.id = V4L2_CID_VIV_EXTCTRL;
    ec.size = 0;
    ecs.controls = &ec;
    ecs.count = 1;

    ret = ioctl(m_fd, VIDIOC_G_EXT_CTRLS, &ecs);
    if (ret != 0) {
        ALOGV("%s: ret %d, line %d", __func__, ret, __LINE__);
    }
    strcpy(ec.string, jsonRequest.toStyledString().c_str());

    ret = ioctl(m_fd, VIDIOC_S_EXT_CTRLS, &ecs);
    if (ret != 0) {
        ALOGI("%s: ret %d, line %d", __func__, ret, __LINE__);
        goto failed;
    }
    ret = ioctl(m_fd, VIDIOC_G_EXT_CTRLS, &ecs);
    if (ret != 0) {
        ALOGV("%s: ret %d, line %d", __func__, ret, __LINE__);
    }

    if (!reader.parse(ec.string, jsonResponse, true)) {
        ALOGE("Could not parse configuration file: %s",
          reader.getFormattedErrorMessages().c_str());
        ret = BAD_VALUE;
        goto failed;
    } else
        ret = jsonResponse["MC_RET"].asInt();

failed:
    delete ec.string;
    ec.string = NULL;
    return ret;
}

#define AE_ENABLE_PARAMS    "enable"
#define IF_AE_S_EN          "ae.s.en"

#ifndef NS_PER_SEC
#define NS_PER_SEC  1000000000
#endif

void ISPWrapper::getExpGainBoundary()
{
    Json::Value jRequest, jResponse;
    int ret = viv_private_ioctl(IF_EC_G_CFG, jRequest, jResponse);
    if (ret == 0) {
        m_ec_gain_min = jResponse[EC_GAIN_MIN_PARAMS].asDouble();
        m_ec_gain_max = jResponse[EC_GAIN_MAX_PARAMS].asDouble();
        ALOGI("%s: minGain %f, maxGain %f", __func__, m_ec_gain_min, m_ec_gain_max);
    } else {
        ALOGE("%s: EC gain get failed", __func__);
    }

    if ((ret != 0) || (m_ec_gain_min == m_ec_gain_max)) {
        m_ec_gain_min = EXP_GAIN_MIN_DFT;
        m_ec_gain_max = EXP_GAIN_MAX_DFT;
    }

    return;
}

#define GAIN_LEVEL_MIN 1
#define GAIN_LEVEL_MAX 10

int ISPWrapper::processExposureGain(int32_t gain, bool force)
{
    int ret;
    Json::Value jRequest, jResponse;

    if((m_exposure_gain == gain) && (force == false))
        return 0;

    if(gain > GAIN_LEVEL_MAX) gain = GAIN_LEVEL_MAX;
    if(gain < GAIN_LEVEL_MIN) gain = GAIN_LEVEL_MIN;

    // first disable aec
    processAeMode(ANDROID_CONTROL_AE_MODE_OFF);

    // calc the value to set
    double exposure_gain = m_ec_gain_min + ((gain - GAIN_LEVEL_MIN) * (m_ec_gain_max - m_ec_gain_min)) / (GAIN_LEVEL_MAX - GAIN_LEVEL_MIN);

    // If never set exposure time, use default value.
    if(m_exposure_time == 0)
        m_exposure_time = EXP_TIME_DFT_NS;

    double exposure_second = (double)m_exposure_time/NS_PER_SEC;

    jRequest[EC_GAIN_PARAMS] = exposure_gain;
    jRequest[EC_TIME_PARAMS] = exposure_second;

    ALOGI("%s: change gain from %d to %d, set exposure gain to %f, exposure time to %f, force %d",
        __func__, m_exposure_gain, gain, exposure_gain, exposure_second, force);

    ret = viv_private_ioctl(IF_EC_S_CFG, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: viv_private_ioctl failed, ret %d", __func__, ret);
        return ret;
    }

    m_exposure_gain = gain;

    return 0;
}

int ISPWrapper::processExposureTime(int64_t exposureNs, bool force)
{
    int ret;
    Json::Value jRequest, jResponse;

    if((m_exposure_time == exposureNs) && (force == false))
        return 0;

    if(exposureNs > m_SensorData->mExposureNsMax) exposureNs = m_SensorData->mExposureNsMax;
    if(exposureNs < m_SensorData->mExposureNsMin) exposureNs = m_SensorData->mExposureNsMin;

    // first disable aec
    processAeMode(ANDROID_CONTROL_AE_MODE_OFF);

    // If never set gain, use default comp vaule 0.
    if ((m_exposure_comp < m_SensorData->mAeCompMin) || (m_exposure_comp > m_SensorData->mAeCompMax))
        m_exposure_comp = 0;

    double gain = m_ec_gain_min + ((m_exposure_comp - m_SensorData->mAeCompMin) * (m_ec_gain_max - m_ec_gain_min)) / (m_SensorData->mAeCompMax - m_SensorData->mAeCompMin);
    double exposure_second = (double)exposureNs/NS_PER_SEC;

    jRequest[EC_GAIN_PARAMS] = gain;
    jRequest[EC_TIME_PARAMS] = exposure_second;

    ALOGI("%s: change exposureNs from %ld to %ld, set exposure gain to %f, exposure time to %f, force %d",
        __func__, m_exposure_time, exposureNs, gain, exposure_second, force);

    ret = viv_private_ioctl(IF_EC_S_CFG, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: viv_private_ioctl failed, ret %d", __func__, ret);
        return ret;
    }

    m_exposure_time = exposureNs;

    return 0;
}

int ISPWrapper::processAeMode(uint8_t mode, bool force)
{
    if((mode != ANDROID_CONTROL_AE_MODE_OFF) && (mode != ANDROID_CONTROL_AE_MODE_ON)) {
        ALOGW("%s: unsupported ae mode %d", __func__, mode);
        return BAD_VALUE;
    }

    ImxStream *imxStream = (ImxStream *)m_stream;
    if (imxStream->isPictureIntent() && (mode != ANDROID_CONTROL_AE_MODE_OFF)) {
        ALOGW("%s: Taking picture only support ae off", __func__);
        return 0;
    }

    if((mode == m_ae_mode) && (force == false))
        return 0;

    ALOGI("%s: set ae mode to %d, force %d", __func__, mode, force);

    bool enable = (mode == ANDROID_CONTROL_AE_MODE_ON);
    Json::Value jRequest, jResponse;
    jRequest[AE_ENABLE_PARAMS] = enable;

    int ret = viv_private_ioctl(IF_AE_S_EN, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: viv_private_ioctl failed, ret %d", __func__, ret);
        return ret;
    }

    m_ae_mode = mode;

    // m_exposure_comp is only valid when aec is off. When aec is on, set it to an invalid value.
    // So when use manual aec, the comp will be set whatever.
    // If not so, when (aec off, comp = max) -> (aec on) -> (aec off, comp = max),
    // processExposureGain() will do nothing since comp not change.
    if(m_ae_mode == ANDROID_CONTROL_AE_MODE_ON) {
        m_exposure_comp = m_SensorData->mAeCompMax + 1;
        m_exposure_time = 0.0;
    }

    return 0;
}

void ISPWrapper::parseDewarpParams(Json::Value& node) {
    Json::Value item;

    // parse mode
    item = node["mode"];
    if (!item.isNull())
        m_dwePara.mode = item.asInt();

    // parse hflip
    item = node["hflip"];
    if (!item.isNull())
        m_dwePara.hflip = item.asBool();

    // parse vflip
    item = node["vflip"];
    if (!item.isNull())
        m_dwePara.vflip = item.asBool();

    // parse bypass
    item = node["bypass"];
    if (!item.isNull())
        m_dwePara.bypass = item.asBool();

    // parse mat
    Json::Value cameraMat = node["mat"];
    if (!cameraMat.isArray())
        return;

    int i = 0;
    for (auto& item : cameraMat) {
        if (i >= MAT_SIZE)
            break;
        m_dwePara.mat[i++] = item.asDouble();
    }


    ALOGI("%s: mode %d, hflip %d, vflip %d, bypass %d", __func__, m_dwePara.mode, m_dwePara.hflip, m_dwePara.vflip, m_dwePara.bypass);
    for (int i = 0; i < MAT_SIZE; i++)
        ALOGI("mat[%d] %f\n", i, m_dwePara.mat[i]);

    return;
}

int ISPWrapper::setDewarpParams() {
    Json::Value jRequest, jResponse;

    jRequest["dwe"]["mode"] = m_dwePara.mode;
    jRequest["dwe"]["hflip"] = m_dwePara.hflip;
    jRequest["dwe"]["vflip"] = m_dwePara.vflip;
    jRequest["dwe"]["bypass"] = m_dwePara.bypass;
    for (int i = 0; i < MAT_SIZE; i++) {
        jRequest["dwe"]["mat"][i] = m_dwePara.mat[i];
    }

    int ret = viv_private_ioctl(IF_DWE_S_PARAMS, jRequest, jResponse);
    return ret;
}

int ISPWrapper::processDewarp(bool bEnable)
{
    int ret = 0;
    int orgMode = m_dwePara.mode;

    if(bEnable == true) {
        if(orgMode == DEWARP_MODEL_FISHEYE_DEWARP)
            return 0;

        m_dwePara.mode = DEWARP_MODEL_FISHEYE_DEWARP;
        ret = setDewarpParams();
        if(ret) {
            m_dwePara.mode = orgMode;
            ALOGE("%s, set DEWARP_MODEL_FISHEYE_DEWARP failed, ret %d", __func__, ret);
            return BAD_VALUE;
        }
    } else {
        if(orgMode == DEWARP_MODEL_LENS_DISTORTION_CORRECTION)
            return 0;

        m_dwePara.mode = DEWARP_MODEL_LENS_DISTORTION_CORRECTION;
        ret = setDewarpParams();
        if(ret) {
            m_dwePara.mode = orgMode;
            ALOGE("%s, set DEWARP_MODEL_LENS_DISTORTION_CORRECTION failed, ret %d", __func__, ret);
            return BAD_VALUE;
        }
    }

    return 0;
}

int ISPWrapper::processHFlip(bool bEnable)
{
    int ret = 0;
    bool orgHFlip = m_dwePara.hflip;

    if (bEnable == orgHFlip)
        return 0;

    m_dwePara.hflip = bEnable;
    ret = setDewarpParams();
    if(ret) {
        m_dwePara.hflip = orgHFlip;
        return BAD_VALUE;
    }

    return 0;
}

int ISPWrapper::processVFlip(bool bEnable)
{
    int ret = 0;
    bool orgVFlip = m_dwePara.vflip;

    if (bEnable == orgVFlip)
        return 0;

    m_dwePara.vflip = bEnable;
    ret = setDewarpParams();
    if(ret) {
        m_dwePara.vflip = orgVFlip;
        return BAD_VALUE;
    }

    return 0;
}

int ISPWrapper::processLSC(bool bEnable)
{
    int ret = 0;

    if (bEnable == mLSCEnable)
        return 0;

    Json::Value jRequest, jResponse;
    jRequest[LSC_ENABLE_PARAMS] = bEnable;

    ret = viv_private_ioctl(IF_LSC_S_EN, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: viv_private_ioctl failed, ret %d", __func__, ret);
        return ret;
    }

    mLSCEnable = bEnable;
    return 0;
}

template<typename T>
void writeArrayToNode(const T *array, Json::Value& node, const char *section, int size) {
    for (int i = 0; i < size; i ++) {
        node[section][i] = array[i];
    }
}
#define JH_GET_TYPE(x) std::remove_reference<decltype((x))>::type
#define addArray(x, y, z) writeArrayToNode<JH_GET_TYPE((x)[0])>(x, y, z, sizeof(x)/sizeof((x)[0]));
#define GAMMA_MIN   (float)1.0
#define GAMMA_MAX   (float)5.0

int ISPWrapper::processGamma(float gamma)
{
    int ret = 0;

    if ((gamma < GAMMA_MIN) || (gamma > GAMMA_MAX)) {
        ALOGW("%s: unsupported gamma %f", __func__, gamma);
        return BAD_VALUE;
    }

    if (gamma == m_gamma)
        return 0;

    uint16_t curve[17] = {0};
    uint16_t gamma_x_equ[16] = {256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256};
    uint16_t gamma_x_log[16] = {64, 64, 64, 64, 128, 128, 128, 128, 256, 256, 256, 256, 512, 512, 512, 512};
    uint16_t *pTable;

    Json::Value jRequest, jResponse;
    ret = viv_private_ioctl(IF_GC_G_CFG, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: viv_private_ioctl IF_GC_G_CFG failed, ret %d", __func__, ret);
        return ret;
    }

    int mode = 0;
    Json::Value item = jResponse[GC_MODE_PARAMS];
    if (!item.isNull())
        mode = item.asInt();

    jRequest = jResponse;
    float dinvgamma = 1.0f/gamma;
    float sumx = 0;
    pTable = mode == 1 ? gamma_x_log : gamma_x_equ;

    for(int i = 0; i < 16; i++) {
        sumx += pTable[i];
        curve[i+1]= std::min(1023.0f, std::max(0.f, pow(((float)sumx)/4096.0f, dinvgamma) * 1024));
    }

    addArray(curve, jRequest, GC_CURVE_PARAMS);
    ret = viv_private_ioctl(IF_GC_S_CURVE, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: viv_private_ioctl IF_GC_S_CURVE failed, ret %d", __func__, ret);
        return ret;
    }

    m_gamma = gamma;

		return 0;
}


int ISPWrapper::processBrightness(int brightness)
{
    int ret = 0;

    if ((brightness < BRIGHTNESS_MIN) || (brightness > BRIGHTNESS_MAX)) {
        ALOGW("%s: unsupported brightness %d", __func__, brightness);
        return BAD_VALUE;
    }

    if (brightness == m_brightness)
        return 0;

    Json::Value jRequest, jResponse;
    ret = viv_private_ioctl(IF_CPROC_G_CFG, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: viv_private_ioctl IF_CPROC_G_CFG failed, ret %d", __func__, ret);
        return ret;
    }

    jRequest = jResponse;
    jRequest[CPROC_BRIGHTNESS_PARAMS] = brightness;
    ret = viv_private_ioctl(IF_CPROC_S_CFG, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: viv_private_ioctl IF_CPROC_S_CFG failed, ret %d", __func__, ret);
        return ret;
    }

    m_brightness = brightness;

    return 0;
}

int ISPWrapper::processContrast(float contrast)
{
    int ret = 0;

    if ((contrast < CONTRAST_MIN) || (contrast > CONTRAST_MAX)) {
        ALOGW("%s: unsupported contrast %f", __func__, contrast);
        return BAD_VALUE;
    }

    if (contrast == m_contrast)
        return 0;

    Json::Value jRequest, jResponse;
    ret = viv_private_ioctl(IF_CPROC_G_CFG, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: viv_private_ioctl IF_CPROC_G_CFG failed, ret %d", __func__, ret);
        return ret;
    }

    jRequest = jResponse;
    jRequest[CPROC_CONTRAST_PARAMS] = contrast;
    ret = viv_private_ioctl(IF_CPROC_S_CFG, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: viv_private_ioctl IF_CPROC_S_CFG failed, ret %d", __func__, ret);
        return ret;
    }

    m_contrast = contrast;

    return 0;
}

int ISPWrapper::processSaturation(float saturation)
{
    int ret = 0;

    if ((saturation < SATURATION_MIN) || (saturation > SATURATION_MAX)) {
        ALOGW("%s: unsupported saturation %f", __func__, saturation);
        return BAD_VALUE;
    }

    if (saturation == m_saturation)
        return 0;

    Json::Value jRequest, jResponse;
    ret = viv_private_ioctl(IF_CPROC_G_CFG, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: viv_private_ioctl IF_CPROC_G_CFG failed, ret %d", __func__, ret);
        return ret;
    }

    jRequest = jResponse;
    jRequest[CPROC_SATURATION_PARAMS] = saturation;
    ret = viv_private_ioctl(IF_CPROC_S_CFG, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: viv_private_ioctl IF_CPROC_S_CFG failed, ret %d", __func__, ret);
        return ret;
    }

    m_saturation = saturation;

    return 0;
}

int ISPWrapper::processHue(int hue)
{
    int ret = 0;

    if ((hue < HUE_MIN) || (hue > HUE_MAX)) {
        ALOGW("%s: unsupported hue %d", __func__, hue);
        return BAD_VALUE;
    }

    if (hue == m_hue)
        return 0;

    Json::Value jRequest, jResponse;
    ret = viv_private_ioctl(IF_CPROC_G_CFG, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: viv_private_ioctl IF_CPROC_G_CFG failed, ret %d", __func__, ret);
        return ret;
    }

    jRequest = jResponse;
    jRequest[CPROC_HUE_PARAMS] = hue;
    ret = viv_private_ioctl(IF_CPROC_S_CFG, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: viv_private_ioctl IF_CPROC_S_CFG failed, ret %d", __func__, ret);
        return ret;
    }

    m_hue = hue;

    return 0;
}

int ISPWrapper::processSharpLevel(uint8_t level)
{
    int ret = 0;

    if ((level < SHARP_LEVEL_MIN) || (level > SHARP_LEVEL_MAX)) {
        ALOGW("%s: unsupported sharp level %d", __func__, level);
        return BAD_VALUE;
    }

    if (level == m_sharp_level)
        return 0;

    // disable filter
    Json::Value jRequest, jResponse;
    jRequest[FILTER_ENABLE_PARAMS] = false;
    ret = viv_private_ioctl(IF_FILTER_S_EN, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: viv_private_ioctl IF_FILTER_S_EN false failed, ret %d", __func__, ret);
        return ret;
    }

    // set manual mode and sharp level
    ret = viv_private_ioctl(IF_FILTER_G_CFG, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: viv_private_ioctl IF_FILTER_G_CFG failed, ret %d", __func__, ret);
        return ret;
    }

    jRequest[FILTER_AUTO_PARAMS] = false;
    jRequest[FILTER_SHARPEN_PARAMS] = level;
    ret = viv_private_ioctl(IF_FILTER_S_CFG, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: viv_private_ioctl IF_FILTER_S_CFG failed, ret %d", __func__, ret);
        return ret;
    }

    // enable filter
    jRequest[FILTER_ENABLE_PARAMS] = true;
    ret = viv_private_ioctl(IF_FILTER_S_EN, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: viv_private_ioctl IF_FILTER_S_EN true failed, ret %d", __func__, ret);
        return ret;
    }

    m_sharp_level = level;

    return 0;
}

void ISPWrapper::getLatestExpWB()
{
    int ret = 0;
    Json::Value jRequest, jResponse;

    ret = viv_private_ioctl(IF_EC_G_CFG, jRequest, jResponse);
    if (ret == 0) {
        m_last_exposure_gain = jResponse[EC_GAIN_PARAMS].asDouble();
        m_last_exposure_time = jResponse[EC_TIME_PARAMS].asDouble();
        ALOGI("%s: exposure -- gain %f, time %f", __func__, m_last_exposure_gain, m_last_exposure_time);
    } else {
        ALOGE("%s: IF_EC_G_CFG failed, ret %d", __func__, ret);
    }

    ret = viv_private_ioctl(IF_WB_G_CFG, jRequest, jResponse);
    if (ret == 0) {
        m_last_wb_r =  jResponse[WB_RED_PARAMS].asFloat();
        m_last_wb_gr =  jResponse[WB_GREEN_R_PARAMS].asFloat();
        m_last_wb_gb =  jResponse[WB_GREEN_B_PARAMS].asFloat();
        m_last_wb_b =  jResponse[WB_BLUE_PARAMS].asFloat();
        ALOGI("%s: wb -- r %f, gr %f, gb %f, b %f", __func__, m_last_wb_r, m_last_wb_gr, m_last_wb_gb, m_last_wb_b);
    } else {
        ALOGE("%s: IF_WB_G_CFG failed, ret %d", __func__, ret);
    }

    return;
}

int ISPWrapper::setExposure(double gain, double time)
{
    int ret;
    Json::Value jRequest, jResponse;

    jRequest[EC_GAIN_PARAMS] = gain;
    jRequest[EC_TIME_PARAMS] = time;

    ret = viv_private_ioctl(IF_EC_S_CFG, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: IF_EC_S_CFG failed, ret %d", __func__, ret);
        return ret;
    }

    return 0;
}

int ISPWrapper::setWB(float r, float gr, float gb, float b)
{
   Json::Value jRequest, jResponse;

    jRequest[WB_RED_PARAMS]     = r;
    jRequest[WB_GREEN_R_PARAMS] = gr;
    jRequest[WB_GREEN_B_PARAMS] = gb;
    jRequest[WB_BLUE_PARAMS]    = b;

    int ret = viv_private_ioctl(IF_WB_S_GAIN, jRequest, jResponse);
    if(ret) {
        ALOGE("%s, IF_WB_S_GAIN ret %d", __func__, ret);
        return BAD_VALUE;
    }

    return 0;
}

int ISPWrapper::recoverExpWB()
{
    ALOGI("enter %s", __func__);

    int ret1 = 0;
    int ret2 = 0;
    int ret3 = 0;
    int ret4 = 0;

    if ((m_last_exposure_gain > 0.0) && (m_last_exposure_time > 0.0)) {
        ret1 = processAeMode(ANDROID_CONTROL_AE_MODE_OFF, true);

        ALOGI("%s: call setExposure, gain %f, time %f", __func__, m_last_exposure_gain, m_last_exposure_time);
        ret2 = setExposure(m_last_exposure_gain, m_last_exposure_time);
    }

    if ((m_last_wb_r > 0.0) && (m_last_wb_gr > 0.0)  && (m_last_wb_gb > 0.0)  && (m_last_wb_b > 0.0)) {
        ret3 = processAWB(ANDROID_CONTROL_AWB_MODE_OFF, true);

        ALOGI("%s: call setWB, r %f, gr %f, gb %f, b %f",
            __func__, m_last_wb_r, m_last_wb_gr, m_last_wb_gb, m_last_wb_b);
        ret4 = setWB(m_last_wb_r, m_last_wb_gr, m_last_wb_gb, m_last_wb_b);
    }

    if (ret1 || ret2 || ret3 || ret4) {
        ALOGE("%s: failed, ret1 %d, ret2 %d, ret3 %d, ret4 %d", __func__, ret1, ret2, ret3, ret4);
        return BAD_VALUE;
    }

    return 0;
}

void ISPWrapper::dump()
{
    getLatestExpWB();
}

}  // namespace android
