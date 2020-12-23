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
#define STR_AWB_INCANDESCENT (char *)"{<id>:<awb.s.gain>; <red>:1.09915;<green.b>:1.0;<green.r>:1.0;<blue>:3.1024}"
#define STR_AWB_FLUORESCENT (char *)"{<id>:<awb.s.gain>; <red>:1.58448;<green.b>:1.0;<green.r>:1.0;<blue>:2.5385}"
#define STR_AWB_WARM_FLUORESCENT (char *)"{<id>:<awb.s.gain>; <red>:1.28448;<green.b>:1.2;<green.r>:1.2;<blue>:2.1385}"
#define STR_AWB_DAYLIGHT (char *)"{<id>:<awb.s.gain>; <red>:1.66425;<green.b>:1.0;<green.r>:1.0;<blue>:1.9972}"
#define STR_AWB_CLOUDY_DAYLIGHT (char *)"{<id>:<awb.s.gain>; <red>:1.94499;<green.b>:1.0;<green.r>:1.0;<blue>:1.6718}"
#define STR_AWB_TWILIGHT (char *)"{<id>:<awb.s.gain>; <red>:1.36191;<green.b>:1.0;<green.r>:1.0;<blue>:2.4337}"
// To be refine once get the data, currently just copy STR_AWB_TWILIGHT
#define STR_AWB_SHADE (char *)"{<id>:<awb.s.gain>; <red>:1.36191;<green.b>:1.0;<green.r>:1.0;<blue>:2.4337}"

// Keep same sequence as camera_metadata_enum_android_control_awb_mode_t defined in camera_metadata_tags.h
static char* g_strWBList[] = {
    STR_AWB_DISABLE,
    STR_AWB_ENABLE,
    STR_AWB_INCANDESCENT,
    STR_AWB_FLUORESCENT,
    STR_AWB_WARM_FLUORESCENT,
    STR_AWB_DAYLIGHT,
    STR_AWB_CLOUDY_DAYLIGHT,
    STR_AWB_TWILIGHT,
    STR_AWB_SHADE
};

namespace android {

using google_camera_hal::HalCameraMetadata;
using cameraconfigparser::CameraSensorMetadata;

class ISPWrapper
{
public:
    ISPWrapper(CameraSensorMetadata *pSensorData);
    ~ISPWrapper();
    int init(char *devPath);
    int process(HalCameraMetadata *pMeta);
    int processAWB(uint8_t mode);
    int processAeMode(uint8_t mode);

private:
    int setFeature(const char *value);
    int viv_private_ioctl(const char *cmd, Json::Value& jsonRequest, Json::Value& jsonResponse);
    int processExposureGain(int32_t comp);

private:
    int m_fd;
    uint32_t m_ctrl_id;
    CameraSensorMetadata *m_SensorData;
    uint8_t m_awb_mode;
    uint8_t m_ae_mode;
    int32_t m_exposure_comp;
    double m_exposure_time;
};

} // namespace android

#endif // _ISP_WRAPPER_H
