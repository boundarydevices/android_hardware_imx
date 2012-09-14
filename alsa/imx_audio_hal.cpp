/*
 * Copyright (C) 2011 The Android Open Source Project
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
/* Copyright (C) 2012 Freescale Semiconductor, Inc. */

#define LOG_TAG "legacy_audio_hw_hal"
//#define LOG_NDEBUG 0

#include <stdint.h>
#include <stdlib.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>

#include <cutils/log.h>
#include <cutils/properties.h>

namespace android_audio_legacy {

extern "C" {

#define AUDIO_HARDWARE_MODULE_ID_LEGACY   "audio.legacy"
#define AUDIO_HARDWARE_MODULE_ID_TINYALSA  "audio.tinyalsa"

struct imx_audio_module {
    struct audio_module module;
};


static int imx_adev_open(const hw_module_t* module, const char* name,
                            hw_device_t** device)
{
    int ret = 0;
    const hw_module_t       *module_audio;
    char value[PROPERTY_VALUE_MAX];
    bool found = false;

    /*find the audio hal for different board*/
    ret = hw_get_module(AUDIO_HARDWARE_MODULE_ID_TINYALSA, &module_audio);
    if(ret)   found = false;
    else {
        ret = module_audio->methods->open(module, AUDIO_HARDWARE_INTERFACE,(struct hw_device_t**)device);
        if(ret)  found = false;
        else     found = true;
    }

    if(!found) {
        ALOGW("reload the legacy audio hal");
        ret = hw_get_module(AUDIO_HARDWARE_MODULE_ID_LEGACY, &module_audio);
        if(ret)
            goto out;

        ret = module_audio->methods->open(module, AUDIO_HARDWARE_INTERFACE,(struct hw_device_t**)device);
        if(ret)
            goto out;
    }

out:
    return ret;
}

static struct hw_module_methods_t imx_audio_module_methods = {
        open: imx_adev_open
};

struct imx_audio_module HAL_MODULE_INFO_SYM = {
    module: {
        common: {
            tag: HARDWARE_MODULE_TAG,
            version_major: 1,
            version_minor: 0,
            id: AUDIO_HARDWARE_MODULE_ID,
            name: "LEGACY Audio HW HAL",
            author: "The Android Open Source Project",
            methods: &imx_audio_module_methods,
            dso : NULL,
            reserved : {0},
        },
    },
};

}; // extern "C"

}; // namespace android_audio_legacy
