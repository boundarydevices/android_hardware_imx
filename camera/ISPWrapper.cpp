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

ISPWrapper::ISPWrapper(CameraSensorMetadata *pSensorData)
{
    m_fd = -1;
    m_ctrl_id = 0;
    m_SensorData = pSensorData;

    // Set ISP feature to it's default value
    m_awb_mode = ANDROID_CONTROL_AWB_MODE_AUTO;
    m_ae_mode = ANDROID_CONTROL_AE_MODE_ON;

    // When init, aec is on. m_exposure_comp is only valid when aec is off, set to an invalid value.
    m_exposure_comp = m_SensorData->mAeCompMax + 1;
    m_exposure_time = 0.0;

    memset(&m_dwePara, 0, sizeof(m_dwePara));
    // Align with daA3840_30mc_1080P.json
    m_dwePara.mode = DEWARP_MODEL_LENS_DISTORTION_CORRECTION;
    m_dwe_on = true;

    m_ec_gain_min = -1.0;
    m_ec_gain_max = -1.0;
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

int ISPWrapper::processAWB(uint8_t mode, bool force)
{
    int ret = 0;
    char *value = NULL;

    ALOGV("%s, mode %d, force %d", __func__, mode, force);

    if(mode >= ARRAY_SIZE(g_strWBList)) {
        ALOGW("%s, unsupported awb mode %d", __func__, mode);
        return BAD_VALUE;
    }

    if((mode == m_awb_mode) && (force == false))
        return 0;

    ALOGI("%s, change WB mode from %d to %d, force %d", __func__, m_awb_mode, mode, force);

    // If shift from AWB to MWB, first disable AWB.
    if( (m_awb_mode == ANDROID_CONTROL_AWB_MODE_AUTO) &&
        (mode != ANDROID_CONTROL_AWB_MODE_AUTO) &&
        (mode != ANDROID_CONTROL_AWB_MODE_OFF) ) {
        value = STR_AWB_DISABLE;
        ret = setFeature(value);
        if(ret) {
            ALOGE("%s, mode %d, disable awb failed", __func__, mode);
            return BAD_VALUE;
        }
    }

    value = g_strWBList[mode];

    ret = setFeature(value);
    if(ret) {
        ALOGE("%s, set wb mode %d failed, ret %d", __func__, mode, ret);
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

    ret = pMeta->Get(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &entry);
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

#if 0
    // The com.intermedia.hd.camera.professional.fbnps_8730133319.apk enalbes aec right
    // after set exposure value, so the brightness will recover very quickly, hard to demo
    // the effect. Will uncomment the code after find a suitable APK.

    ret = pMeta->Get(ANDROID_CONTROL_AE_MODE, &entry);
    if(ret == 0)
        processAeMode(entry.data.u8[0]);
#endif

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
        ALOGV("==== ret %, line %d", ret, __LINE__);
  //      goto failed;
    }
    strcpy(ec.string, jsonRequest.toStyledString().c_str());

    ret = ioctl(m_fd, VIDIOC_S_EXT_CTRLS, &ecs);
    if (ret != 0) {
        ALOGI("==== ret %, line %d", ret, __LINE__);
        goto failed;
    }
    ret = ioctl(m_fd, VIDIOC_G_EXT_CTRLS, &ecs);
    if (ret != 0) {
        ALOGV("==== ret %, line %d", ret, __LINE__);
      //  goto failed;
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

#define NS_PER_SEC  1000000000

void ISPWrapper::getExpGainBoundary()
{
    Json::Value jRequest, jResponse;
    double minGain, maxGain, currentGain, currentInt;
    int ret = viv_private_ioctl(IF_EC_G_CFG, jRequest, jResponse);
    if (ret == 0) {
        m_ec_gain_min = jResponse[EC_GAIN_MIN_PARAMS].asDouble();
        m_ec_gain_max = jResponse[EC_GAIN_MAX_PARAMS].asDouble();
        ALOGI("%s: minGain %f, maxGain %f", __func__, m_ec_gain_min, m_ec_gain_max);
    } else {
        ALOGE("%s: EC gain get failed", __func__);
    }

    return;
}

int ISPWrapper::processExposureGain(int32_t comp, bool force)
{
    int ret;
    Json::Value jRequest, jResponse;

    if((m_exposure_comp == comp) && (force == false))
        return 0;

    if(comp > m_SensorData->mAeCompMax) comp = m_SensorData->mAeCompMax;
    if(comp < m_SensorData->mAeCompMin) comp = m_SensorData->mAeCompMin;

    // first disable aec
    processAeMode(ANDROID_CONTROL_AE_MODE_OFF);

    // Fix me, currntly just linear map compensation value to gain.
    // In theory, should use (step * value) to calculate the exposure value.
    // Ref https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#CONTROL_AE_COMPENSATION_STEP.
    // One unit of EV compensation changes the brightness of the captured image by a factor of two.
    // +1 EV doubles the image brightness, while -1 EV halves the image brightness.
    // But we don't know how much gain will double brightness.
    double gain = m_ec_gain_min + ((comp - m_SensorData->mAeCompMin) * (m_ec_gain_max - m_ec_gain_min)) / (m_SensorData->mAeCompMax - m_SensorData->mAeCompMin);

    // If never set exposure time, use default value.
    if(m_exposure_time == 0)
        m_exposure_time = EXP_TIME_DFT_NS;

    double exposure_second = (double)m_exposure_time/NS_PER_SEC;

    jRequest[EC_GAIN_PARAMS] = gain;
    jRequest[EC_TIME_PARAMS] = exposure_second;

    ALOGI("%s: change comp from %d to %d, set exposure gain to %f, exposure time to %f, force %d",
        __func__, m_exposure_comp, comp,  gain, exposure_second, force);

    ret = viv_private_ioctl(IF_EC_S_CFG, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: viv_private_ioctl failed, ret %d", __func__, ret);
        return ret;
    }

    m_exposure_comp = comp;

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

    ALOGI("%s: change exposureNs from %d to %d, set exposure gain to %f, exposure time to %f, force %d",
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

}  // namespace android
