/*
 * Copyright (C) 2022 The Android Open Source Project
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

#ifndef ANDROID_HWC_COMMON_H
#define ANDROID_HWC_COMMON_H

#include <inttypes.h>

#include <string>
#include <unordered_map>

#define ATRACE_TAG (ATRACE_TAG_GRAPHICS | ATRACE_TAG_HAL)

#undef LOG_TAG
#define LOG_TAG "NXPHWC"

#include <aidl/android/hardware/graphics/composer3/IComposerClient.h>
#include <android-base/logging.h>
#include <log/log.h>
#include <utils/Trace.h>

// Uncomment to enable additional debug logging.
// #define DEBUG_NXP_HWC

#if defined(DEBUG_NXP_HWC)
#define DEBUG_LOG ALOGI
#else
#define DEBUG_LOG(...) ((void)0)
#endif

#if 0 // Below already defined in Memory.h
#define ALIGN_PIXEL_2(x) ((x + 1) & ~1)
#define ALIGN_PIXEL_4(x) ((x + 3) & ~3)
#define ALIGN_PIXEL_8(x) ((x + 7) & ~7)
#define ALIGN_PIXEL_16(x) ((x + 15) & ~15)
#define ALIGN_PIXEL_32(x) ((x + 31) & ~31)
#define ALIGN_PIXEL_64(x) ((x + 63) & ~63)
#define ALIGN_PIXEL_256(x) ((x + 255) & ~255)
#endif

// Below already defined in system/core/include/cutils/properties.h
// #define PROPERTY_VALUE_MAX 92

namespace aidl::android::hardware::graphics::composer3::impl {

enum {
    UI_SCALE_NONE = 0,
    UI_SCALE_SOFTWARE = 1,
    UI_SCALE_HARDWARE = 2,
};

struct HalDisplayConfig {
    uint32_t width;
    uint32_t height;
    uint32_t dpiX;
    uint32_t dpiY;
    uint32_t refreshRateHz;
    uint32_t blobId;
    uint32_t modeType;
    uint32_t modeWidth;
    uint32_t modeHeight;
};

using HalConfig = std::unordered_map<uint32_t, HalDisplayConfig>;
struct HalMultiConfigs {
    uint32_t displayId;
    int32_t activeConfigId;
    std::shared_ptr<HalConfig> configs;
};

bool IsAutoDevice();

bool IsOverlayUserDisabled();
bool Is2DCompositionUserPrefered();
bool customizeGUIResolution(uint32_t& width, uint32_t& height, uint32_t* uiType);
void parseDisplayMode(uint32_t* width, uint32_t* height, uint32_t* vrefresh, uint32_t* prefermode);
bool checkRectOverlap(common::Rect& masked, common::Rect& src);
void mergeRect(common::Rect& masked, common::Rect& src);

namespace HWC3 {
enum class Error : int32_t {
    None = 0,
    BadConfig = aidl::android::hardware::graphics::composer3::IComposerClient::EX_BAD_CONFIG,
    BadDisplay = aidl::android::hardware::graphics::composer3::IComposerClient::EX_BAD_DISPLAY,
    BadLayer = aidl::android::hardware::graphics::composer3::IComposerClient::EX_BAD_LAYER,
    BadParameter = aidl::android::hardware::graphics::composer3::IComposerClient::EX_BAD_PARAMETER,
    NoResources = aidl::android::hardware::graphics::composer3::IComposerClient::EX_NO_RESOURCES,
    NotValidated = aidl::android::hardware::graphics::composer3::IComposerClient::EX_NOT_VALIDATED,
    Unsupported = aidl::android::hardware::graphics::composer3::IComposerClient::EX_UNSUPPORTED,
    SeamlessNotAllowed =
            aidl::android::hardware::graphics::composer3::IComposerClient::EX_SEAMLESS_NOT_ALLOWED,
};
} // namespace HWC3

std::string toString(HWC3::Error error);

inline ndk::ScopedAStatus ToBinderStatus(HWC3::Error error) {
    if (error != HWC3::Error::None) {
        return ndk::ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(error));
    }
    return ndk::ScopedAStatus::ok();
}

constexpr int32_t HertzToPeriodNanos(uint32_t hertz) {
    return 1000 * 1000 * 1000 / hertz;
}

static inline int32_t min(int32_t a, int32_t b) {
    return (a < b) ? a : b;
}

static inline int32_t max(int32_t a, int32_t b) {
    return (a > b) ? a : b;
}

inline bool isRectEmpty(common::Rect& rect) {
    return (rect.right - rect.left <= 0) || (rect.bottom - rect.top <= 0);
}

inline bool rectIntersect(const common::Rect& with, common::Rect& result) {
    result.left = max(result.left, with.left);
    result.top = max(result.top, with.top);
    result.right = min(result.right, with.right);
    result.bottom = min(result.bottom, with.bottom);
    return !isRectEmpty(result);
}

} // namespace aidl::android::hardware::graphics::composer3::impl

#endif
