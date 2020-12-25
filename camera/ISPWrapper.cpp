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
}

ISPWrapper::~ISPWrapper()
{
    if(m_fd > 0)
        close(m_fd);
}

int ISPWrapper::init(char *devPath)
{
    if(devPath == NULL)
        return BAD_VALUE;

    // already inited
    if(m_ctrl_id > 0)
        return 0;

    int fd = open(devPath, O_RDWR);
    if (fd < 0) {
        ALOGE("%s: open %s failed", __func__, devPath);
        return BAD_VALUE;
    }

    m_fd = fd;

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

int ISPWrapper::processAWB(uint8_t mode)
{
    int ret = 0;
    char *value = NULL;

    ALOGV("%s, mode %d", __func__, mode);

    if(mode >= ARRAY_SIZE(g_strWBList)) {
        ALOGW("%s, unsupported awb mode %d", __func__, mode);
        return BAD_VALUE;
    }

    if(mode == m_awb_mode)
        return 0;

    ALOGI("%s, change WB mode from %d to %d", __func__, m_awb_mode, mode);

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

// Current tactic: don't return if some meta process failed,
// since may have other meta to process.
int ISPWrapper::process(HalCameraMetadata *pMeta)
{
    if(pMeta == NULL)
        return BAD_VALUE;

    status_t ret;
    camera_metadata_ro_entry entry;

    ret = pMeta->Get(ANDROID_CONTROL_AWB_MODE, &entry);
    if(ret == 0)
        processAWB(entry.data.u8[0]);

    ret = pMeta->Get(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &entry);
    if(ret == 0)
        processExposureGain(entry.data.i32[0]);

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
#define EC_TIME_PARAMS    "time"
#define EC_MAX_PARAMS     "max"
#define EC_MIN_PARAMS     "min"
#define EC_STEP_PARAMS    "step"

#define VIV_CUSTOM_CID_BASE   (V4L2_CID_USER_BASE | 0xf000)
#define V4L2_CID_VIV_EXTCTRL  (VIV_CUSTOM_CID_BASE + 1)
#define VIV_JSON_BUFFER_SIZE  (64*1024)

int ISPWrapper::viv_private_ioctl(const char *cmd, Json::Value& jsonRequest, Json::Value& jsonResponse)
{
    if (!cmd) {
        ALOGE("cmd should anot be null!");
        return -1;
    }
    jsonRequest["id"] = cmd;
    jsonRequest["streamid"] = 0;

    struct v4l2_ext_controls ecs;
    struct v4l2_ext_control ec;
    memset(&ecs, 0, sizeof(ecs));
    memset(&ec, 0, sizeof(ec));
    ec.string = new char[VIV_JSON_BUFFER_SIZE];
    ec.id = V4L2_CID_VIV_EXTCTRL;
    ec.size = 0;
    ecs.controls = &ec;
    ecs.count = 1;

    ioctl(m_fd, VIDIOC_G_EXT_CTRLS, &ecs);

    strcpy(ec.string, jsonRequest.toStyledString().c_str());

    int ret = ioctl(m_fd, VIDIOC_S_EXT_CTRLS, &ecs);
    if (ret != 0)
        return ret;

    ioctl(m_fd, VIDIOC_G_EXT_CTRLS, &ecs);

    Json::Reader reader;
    reader.parse(ec.string, jsonResponse, true);
    delete ec.string;
    ec.string = NULL;
    return jsonResponse["MC_RET"].asInt();
}


#define EC_GAIN_MIN   (double)2.900000
#define EC_GAIN_MAX   (double)22.475000

#define AE_ENABLE_PARAMS    "enable"
#define IF_AE_S_EN          "ae.s.en"

#define EXP_TIME_DFT	 0.006535 // unit: seconds

int ISPWrapper::processExposureGain(int32_t comp)
{
    int ret;
    Json::Value jRequest, jResponse;

    if(m_exposure_comp == comp)
        return 0;

    if(comp > m_SensorData->mAeCompMax) comp = m_SensorData->mAeCompMax;
    if(comp < m_SensorData->mAeCompMin) comp = m_SensorData->mAeCompMin;

    // Fix me, currntly just linear map compensation value to gain.
    // In theory, should use (step * value) to calculate the exposure value.
    // Ref https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#CONTROL_AE_COMPENSATION_STEP.
    // One unit of EV compensation changes the brightness of the captured image by a factor of two.
    // +1 EV doubles the image brightness, while -1 EV halves the image brightness.
    // But we don't know how much gain will double brightness.
    double gain = EC_GAIN_MIN + ((comp - m_SensorData->mAeCompMin) * (EC_GAIN_MAX - EC_GAIN_MIN)) / (m_SensorData->mAeCompMax - m_SensorData->mAeCompMin);

    // first disable aec
    processAeMode(ANDROID_CONTROL_AE_MODE_OFF);

    if(m_exposure_time == 0.0) {
        viv_private_ioctl(IF_EC_G_CFG, jRequest, jResponse);
        m_exposure_time = jResponse[EC_TIME_PARAMS].asDouble();
        ALOGI("%s: get exposure time %f", __func__, m_exposure_time);

        if(m_exposure_time == 0.0)
            m_exposure_time = EXP_TIME_DFT;
    }

    jRequest[EC_GAIN_PARAMS] = gain;
    jRequest[EC_TIME_PARAMS] = m_exposure_time;

    ALOGI("%s: change comp from %d to %d, set exposure gain to %f, exposure time to %f",
        __func__, m_exposure_comp, comp,  gain, m_exposure_time);

    ret = viv_private_ioctl(IF_EC_S_CFG, jRequest, jResponse);
    if(ret) {
        ALOGI("%s: viv_private_ioctl failed, ret %d", __func__, ret);
        return ret;
    }

    m_exposure_comp = comp;

    return 0;
}

int ISPWrapper::processAeMode(uint8_t mode)
{
    if((mode != ANDROID_CONTROL_AE_MODE_OFF) && (mode != ANDROID_CONTROL_AE_MODE_ON)) {
        ALOGW("%s: unsupported ae mode %d", __func__, mode);
        return BAD_VALUE;
    }

    if(mode == m_ae_mode)
        return 0;

    ALOGI("%s: set ae mode to %d", __func__, mode);

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

int ISPWrapper::processDewarp(bool bEnable)
{
    int ret = 0;

    if(bEnable == true) {
        if(m_dwePara.mode == DEWARP_MODEL_FISHEYE_DEWARP)
            return 0;

        ret = setFeature(DWE_MODE_DEWARP);
        if(ret) {
            ALOGE("%s, set DWE_MODE_DEWARP failed, ret %d", __func__, ret);
            return BAD_VALUE;
        }
        m_dwePara.mode = DEWARP_MODEL_FISHEYE_DEWARP;
    } else {
        if(m_dwePara.mode == DEWARP_MODEL_LENS_DISTORTION_CORRECTION)
            return 0;

        ret = setFeature(DWE_MODE_LDC);
        if(ret) {
            ALOGE("%s, set DWE_MODE_LDC failed, ret %d", __func__, ret);
            return BAD_VALUE;
        }
        m_dwePara.mode = DEWARP_MODEL_LENS_DISTORTION_CORRECTION;
    }

#include <hal_types.h>
    return 0;
}

int ISPWrapper::processHFlip(bool bEnable)
{
    int ret = 0;

    if (bEnable == m_dwePara.hflip)
        return 0;

    const char *value = bEnable ? DWE_HFLIP_ON : DWE_HFLIP_OFF;

    ret = setFeature(value);
    if(ret)
        return BAD_VALUE;

    m_dwePara.hflip = bEnable;

    return 0;
}

int ISPWrapper::processVFlip(bool bEnable)
{
    int ret = 0;

    if (bEnable == m_dwePara.vflip)
        return 0;

    const char *value = bEnable ? DWE_VFLIP_ON : DWE_VFLIP_OFF;

    ret = setFeature(value);
    if(ret)
        return BAD_VALUE;

    m_dwePara.vflip = bEnable;

    return 0;
}

}  // namespace android
