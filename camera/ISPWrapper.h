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

#ifndef _ISP_WRAPPER_H
#define _ISP_WRAPPER_H

#include <json/json.h>
#include <json/reader.h>
#include <VideoStream.h>

#define STR_AWB_ENABLE    (char *)"{<id>:<awb.s.en>; <enable>:true}"
#define STR_AWB_DISABLE   (char *)"{<id>:<awb.s.en>; <enable>:false}"

// Ref https://support.verisilicon.com/browse/M865SW-411, the MWB gain is got from DAA3840_30MC_1080P.xml.
// INCANDESCENT -> A
// FLUORESCENT -> CWF
// WARM_FLUORESCENT -> U30(3000K)
// DAYLIGHT  -> D50
// CLOUDY_DAYLIGHT  -> D65
// TWILIGHT -> TL84
// SHADE -> ???

#define IF_WB_S_CFG               "wb.s.cfg"
#define IF_WB_G_CFG               "wb.g.cfg"
#define IF_AWB_S_EN               "awb.s.en"
#define WB_GAINS_PARAMS           "wb.gains"
#define AWB_ENABLE_PARAMS          "enable"
#define IF_WB_S_GAIN               "wb.s.gain"
#define WB_RED_PARAMS              "red"
#define WB_GREEN_PARAMS            "green"
#define WB_BLUE_PARAMS             "blue"
#define WB_GREEN_R_PARAMS          "green.r"
#define WB_GREEN_B_PARAMS          "green.b"

typedef struct CamEngineWbGains_s {
      float Red;
      float GreenR;
      float GreenB;
      float Blue;
} WbGains;

#define DWE_MODE_LDC        (char *)"{<id>:<dwe.s.mode>; <dwe>:{<mode>:1}}"
#define DWE_MODE_DEWARP     (char *)"{<id>:<dwe.s.mode>; <dwe>:{<mode>:8}}"
#define DWE_HFLIP_ON        (char *)"{<id>:<dwe.s.hflip>; <dwe>:{<hflip>:true}}"
#define DWE_HFLIP_OFF        (char *)"{<id>:<dwe.s.hflip>; <dwe>:{<hflip>:false}}"
#define DWE_VFLIP_ON        (char *)"{<id>:<dwe.s.vflip>; <dwe>:{<vflip>:true}}"
#define DWE_VFLIP_OFF        (char *)"{<id>:<dwe.s.vflip>; <dwe>:{<vflip>:false}}"

#define IF_DWE_G_PARAMS "dwe.g.params"
#define IF_DWE_S_PARAMS "dwe.s.params"


enum {
    DEWARP_MODEL_LENS_DISTORTION_CORRECTION = 1 << 0,
    DEWARP_MODEL_FISHEYE_EXPAND             = 1 << 1,
    DEWARP_MODEL_SPLIT_SCREEN               = 1 << 2,
    DEWARP_MODEL_FISHEYE_DEWARP             = 1 << 3,
};

#define MAT_SIZE  17
typedef struct DWEPara {
    int mode;
    bool hflip;
    bool vflip;
    bool bypass;
    double mat[MAT_SIZE];
} DWEPara;

#define EXP_TIME_DFT    0.006535 // unit: seconds
#define EXP_TIME_DFT_NS  6535000 // ns

#define EXP_GAIN_MIN_DFT  1.0
#define EXP_GAIN_MAX_DFT  6.879883

#define VIV_VIDIOC_S_CAPS_MODE          _IOW('V',  BASE_VIDIOC_PRIVATE + 9, struct viv_caps_mode_s)

#define CALIBXML_FILE_NAME_SIZE 64
struct viv_caps_mode_s {
  int mode;
  char CalibXmlName[CALIBXML_FILE_NAME_SIZE];
};

#define VIV_VIDIOC_GET_CAPS_SUPPORTS    _IOWR('V', BASE_VIDIOC_PRIVATE + 12, struct viv_caps_supports)
struct viv_caps_mode_info_s{
  unsigned int index;
  unsigned int bounds_width;
  unsigned int bounds_height;
  unsigned int top;
  unsigned int left;
  unsigned int width;
  unsigned int height;
  unsigned int hdr_mode;
  unsigned int stitching_mode;
  unsigned int bit_width;
  unsigned int bayer_pattern;
  unsigned int fps;
};

#define VIV_CAPS_MODE_MAX_COUNT    20
struct viv_caps_supports{
  unsigned int count;
  struct viv_caps_mode_info_s mode[VIV_CAPS_MODE_MAX_COUNT];
};

#define IF_LSC_S_EN         "lsc.s.en"
#define LSC_ENABLE_PARAMS   "enable"

#define IF_GC_G_CFG         "gc.g.cfg"
#define IF_GC_S_CURVE       "gc.s.curve"
#define GC_MODE_PARAMS      "gc.mode"
#define GC_CURVE_PARAMS     "gc.curve"

#define IF_CPROC_G_CFG              "cproc.g.cfg"
#define IF_CPROC_S_CFG              "cproc.s.cfg"
#define CPROC_BRIGHTNESS_PARAMS     "brightness"
#define CPROC_CONTRAST_PARAMS       "contrast"
#define CPROC_SATURATION_PARAMS     "saturation"
#define CPROC_HUE_PARAMS            "hue"

#define BRIGHTNESS_MIN      (int)(-127)
#define BRIGHTNESS_MAX      (int)(127)
#define CONTRAST_MIN        (float)(0.0)
#define CONTRAST_MAX        (float)(1.99)
#define SATURATION_MIN      (float)(0.0)
#define SATURATION_MAX      (float)(1.99)
#define HUE_MIN             (int)(-127)
#define HUE_MAX             (int)(127)

#define IF_FILTER_G_CFG           "filter.g.cfg"
#define IF_FILTER_S_CFG           "filter.s.cfg"
#define FILTER_SHARPEN_PARAMS     "sharpen"
#define SHARP_LEVEL_MIN           (uint8_t)1
#define SHARP_LEVEL_MAX           (uint8_t)10

namespace android {

using google_camera_hal::HalCameraMetadata;
using cameraconfigparser::CameraSensorMetadata;

class ISPWrapper
{
public:
    ISPWrapper(CameraSensorMetadata *pSensorData, void *stream);
    ~ISPWrapper();
    int init(int fd);
    int process(HalCameraMetadata *pMeta, uint32_t format);
    int processAWB(uint8_t mode, bool force = false);
    int processAeMode(uint8_t mode, bool force = false);
    int processExposureGain(int32_t comp, bool force = false);
    int processExposureTime(int64_t exposureNs, bool force = false);
    int64_t getExposureTime() { return m_exposure_time > 0 ? m_exposure_time : EXP_TIME_DFT_NS; }
    int viv_private_ioctl(const char *cmd, Json::Value& jsonRequest, Json::Value& jsonResponse);
    void parseDewarpParams(Json::Value& node);
    void getExpGainBoundary();
    void getLatestExpWB();
    int recoverExpWB();

private:
    int setFeature(const char *value);
    int setDewarpParams();
    int processDewarp(bool bEnable);
    int processHFlip(bool bEnable);
    int processVFlip(bool bEnable);
    int processLSC(bool bEnable);
    int processGamma(float gamma);
    int processBrightness(int brightness);
    int processContrast(float contrast);
    int processSaturation(float saturation);
    int processHue(int hue);
    int processSharpLevel(uint8_t level);

    int EnableDWE(bool on);
    int enableAWB(bool enable);
    int setExposure(double gain, double time);
    int setWB(float r, float gr, float gb, float b);

private:
    int m_fd;
    uint32_t m_ctrl_id;
    CameraSensorMetadata *m_SensorData;
    uint8_t m_awb_mode;
    uint8_t m_ae_mode;
    int32_t m_exposure_comp;
    int32_t m_exposure_gain;
    int64_t m_exposure_time;
    DWEPara m_dwePara;
    bool m_dwe_on;
    double m_ec_gain_min;
    double m_ec_gain_max;
    bool mLSCEnable;
    float m_gamma;

    int m_brightness;
    float m_contrast;
    float m_saturation;
    int m_hue;

    uint8_t m_sharp_level;
    double m_last_exposure_gain;
    double m_last_exposure_time;
    float m_last_wb_r;
    float m_last_wb_gr;
    float m_last_wb_gb;
    float m_last_wb_b;

    void *m_stream; // ISPCameraMMAPStream*
};

} // namespace android

#endif // _ISP_WRAPPER_H
