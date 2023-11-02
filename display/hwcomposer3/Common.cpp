/*
 * Copyright 2022 The Android Open Source Project
 * Copyright 2023 NXP
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

#include "Common.h"

#include <android-base/properties.h>
#include <cutils/properties.h>

namespace aidl::android::hardware::graphics::composer3::impl {

bool IsAutoDevice() {
    // gcar_emu_x86_64, sdk_car_md_x86_64, cf_x86_64_auto, cf_x86_64_only_auto_md
    const std::string product_name = ::android::base::GetProperty("ro.product.name", "");
    return product_name.find("car_") || product_name.find("_auto");
}

bool IsCuttlefish() {
    return ::android::base::GetProperty("ro.product.board", "") == "cutf";
}

bool IsCuttlefishFoldable() {
    return IsCuttlefish() &&
            ::android::base::GetProperty("ro.product.name", "").find("foldable") !=
            std::string::npos;
}

bool IsInNoOpCompositionMode() {
    const std::string mode = ::android::base::GetProperty("ro.vendor.hwcomposer.mode", "");
    DEBUG_LOG("%s: sysprop ro.vendor.hwcomposer.mode is %s", __FUNCTION__, mode.c_str());
    return mode == "noop";
}

bool IsInClientCompositionMode() {
    const std::string mode = ::android::base::GetProperty("ro.vendor.hwcomposer.mode", "");
    DEBUG_LOG("%s: sysprop ro.vendor.hwcomposer.mode is %s", __FUNCTION__, mode.c_str());
    return mode == "client";
}

bool IsInGem5DisplayFinderMode() {
    const std::string mode =
            ::android::base::GetProperty("ro.vendor.hwcomposer.display_finder_mode", "");
    DEBUG_LOG("%s: sysprop ro.vendor.hwcomposer.display_finder_mode is %s", __FUNCTION__,
              mode.c_str());
    return mode == "gem5";
}

bool IsInNoOpDisplayFinderMode() {
    const std::string mode =
            ::android::base::GetProperty("ro.vendor.hwcomposer.display_finder_mode", "");
    DEBUG_LOG("%s: sysprop ro.vendor.hwcomposer.display_finder_mode is %s", __FUNCTION__,
              mode.c_str());
    return mode == "noop";
}

bool IsInDrmDisplayFinderMode() {
    const std::string mode =
            ::android::base::GetProperty("ro.vendor.hwcomposer.display_finder_mode", "");
    DEBUG_LOG("%s: sysprop ro.vendor.hwcomposer.display_finder_mode is %s", __FUNCTION__,
              mode.c_str());
    return mode == "drm";
}

bool IsOverlayUserDisabled() {
    const std::string overlay = ::android::base::GetProperty("vendor.hwc.disable.overlay", "0");
    DEBUG_LOG("%s: sysprop vendor.hwc.disable.overlay is %s", __FUNCTION__, overlay.c_str());
    return overlay == "1";
}

bool Is2DCompositionUserPrefered() {
    const std::string g2d = ::android::base::GetProperty("vendor.hwc.prefer.2d-composition", "1");
    DEBUG_LOG("%s: sysprop vendor.hwc.disable.2d-composition is %s", __FUNCTION__, g2d.c_str());
    return g2d == "1";
}

std::string toString(HWC3::Error error) {
    switch (error) {
        case HWC3::Error::None:
            return "None";
        case HWC3::Error::BadConfig:
            return "BadConfig";
        case HWC3::Error::BadDisplay:
            return "BadDisplay";
        case HWC3::Error::BadLayer:
            return "BadLayer";
        case HWC3::Error::BadParameter:
            return "BadParameter";
        case HWC3::Error::NoResources:
            return "NoResources";
        case HWC3::Error::NotValidated:
            return "NotValidated";
        case HWC3::Error::Unsupported:
            return "Unsupported";
        case HWC3::Error::SeamlessNotAllowed:
            return "SeamlessNotAllowed";
    }
}

bool customizeGUIResolution(uint32_t &width, uint32_t &height, uint32_t *uiType) {
    bool ret = true;
    uint32_t w = 0, h = 0, temp = 0;

    char w_buf[PROPERTY_VALUE_MAX];
    char h_buf[PROPERTY_VALUE_MAX];

    const std::string res = ::android::base::GetProperty("ro.boot.gui_resolution", "p");
    DEBUG_LOG("%s: sysprop ro.boot.gui_resolution is %s", __FUNCTION__, res.c_str());

    const char *value = res.c_str();
    if (!strncmp(value, "shw", 3) && (sscanf(value, "shw%[0-9]x%[0-9]", w_buf, h_buf) == 2)) {
        w = atoi(w_buf);
        h = atoi(h_buf);
        *uiType = UI_SCALE_HARDWARE;
    } else if (!strncmp(value, "ssw", 3) &&
               (sscanf(value, "ssw%[0-9]x%[0-9]", w_buf, h_buf) == 2)) {
        w = atoi(w_buf);
        h = atoi(h_buf);
        *uiType = UI_SCALE_SOFTWARE;
    } else {
        if (!strncmp(value, "4k", 2)) {
            w = 3840;
            h = 2160;
        } else if (!strncmp(value, "1080p", 5)) {
            w = 1920;
            h = 1080;
        } else if (!strncmp(value, "720p", 4)) {
            w = 1280;
            h = 720;
        } else if (!strncmp(value, "480p", 4)) {
            w = 640;
            h = 480;
        } else {
            ret = false;
        }
        if (width < height) {
            temp = w;
            w = h;
            h = temp;
        }
    }
    if (w > 0 && h > 0) {
        if ((w >= width) && (h >= height)) {
            ret = false;
        } else {
            if (w < width)
                width = w;
            if (h < height)
                height = h;
        }
    }

    return ret;
}

struct DisplayMode {
    char modestr[16];
    int width;
    int height;
    int vrefresh;
};

DisplayMode gDisplayModes[16] = {
        {"4kp60", 3840, 2160, 60},   {"4kp50", 3840, 2160, 50},   {"4kp30", 3840, 2160, 30},
        {"1080p60", 1920, 1080, 60}, {"1080p50", 1920, 1080, 50}, {"1080p30", 1920, 1080, 30},
        {"720p60", 1280, 720, 60},   {"720p50", 1280, 720, 50},   {"720p30", 1280, 720, 30},
        {"480p60", 640, 480, 60},    {"480p50", 640, 480, 50},    {"480p30", 640, 480, 30},
        {"4k", 3840, 2160, 60},    // default as 60fps
        {"1080p", 1920, 1080, 60}, // default as 60fps
        {"720p", 1280, 720, 60},   // default as 60fps
        {"480p", 640, 480, 60},    // default as 60fps
};

void parseDisplayMode(uint32_t *width, uint32_t *height, uint32_t *vrefresh, uint32_t *prefermode) {
    char value[PROPERTY_VALUE_MAX];
    memset(value, 0, sizeof(value));
    property_get("ro.boot.displaymode", value, "p");

    int i = 0;
    int modeCount = sizeof(gDisplayModes) / sizeof(DisplayMode);
    *prefermode = 0;
    for (i = 0; i < modeCount; i++) {
        if (!strncmp(value, gDisplayModes[i].modestr, strlen(gDisplayModes[i].modestr))) {
            *width = gDisplayModes[i].width;
            *height = gDisplayModes[i].height;
            *vrefresh = gDisplayModes[i].vrefresh;
            break;
        }
    }

    if (i == modeCount) {
        bool isValid = true;
        char delim[] = "xp";
        int modeResult[3] = {0};
        // displaymode format should be 1920x1080p60
        if (strstr(value, "x") && strstr(value, "p")) {
            char *s = strdup(value);
            char *token;
            for (i = 0, token = strsep(&s, delim); i < 3 && token != NULL;
                 token = strsep(&s, delim), i++) {
                modeResult[i] = atoi(token);
                if (modeResult[i] <= 0) {
                    isValid = false;
                    break;
                }
            }
        } else {
            isValid = false;
        }

        if (isValid) {
            *width = modeResult[0];
            *height = modeResult[1];
            *vrefresh = modeResult[2];
            *prefermode = 0;
        } else {
            // Set default mode as 1080p60
            *width = 1920;
            *height = 1080;
            *vrefresh = 60;
            *prefermode = 1;
        }
    }
}

bool checkRectOverlap(common::Rect &masked, common::Rect &src) { // check if there is overlap or not
    return !(masked.left >= src.right || masked.right <= src.left || masked.top >= src.bottom ||
             masked.bottom <= src.top);
}

void mergeRect(common::Rect &masked, common::Rect &src) {
    masked.left = masked.left <= src.left ? masked.left : src.left;
    masked.right = masked.right >= src.right ? masked.right : src.right;
    masked.top = masked.top <= src.top ? masked.top : src.top;
    masked.bottom = masked.bottom >= src.bottom ? masked.bottom : src.bottom;
}

} // namespace aidl::android::hardware::graphics::composer3::impl
