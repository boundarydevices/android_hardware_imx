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
#include <ui/GraphicBufferMapper.h>

namespace aidl::android::hardware::graphics::composer3::impl {

bool IsAutoDevice() {
    // gcar_emu_x86_64, sdk_car_md_x86_64, cf_x86_64_auto, cf_x86_64_only_auto_md
    const std::string product_name = ::android::base::GetProperty("ro.product.name", "");
    return (product_name.find("car_") != std::string::npos) ||
            (product_name.find("_auto") != std::string::npos);
}

bool IsOverlayUserDisabled() {
    const std::string overlay = ::android::base::GetProperty("vendor.hwc.disable.overlay", "0");
    DEBUG_LOG("%s: sysprop vendor.hwc.disable.overlay is %s", __FUNCTION__, overlay.c_str());
    return overlay == "1";
}

bool Is2DCompositionUserDisabled() {
    const std::string g2d = ::android::base::GetProperty("vendor.sys.hwc.disable", "0");
    DEBUG_LOG("%s: sysprop vendor.sys.hwc.disable is %s", __FUNCTION__, g2d.c_str());
    return g2d == "1";
}

bool Is2DCompositionUserPrefered() {
    const std::string g2d = ::android::base::GetProperty("vendor.hwc.prefer.2d-composition", "1");
    DEBUG_LOG("%s: sysprop vendor.hwc.prefer.2d-composition is %s", __FUNCTION__, g2d.c_str());
    return g2d == "1";
}

bool IsHdcpUserEnabled() {
    const std::string hdcp = ::android::base::GetProperty("ro.boot.support_hdcp", "");
    DEBUG_LOG("%s: sysprop ro.boot.support_hdcp is %s", __FUNCTION__, hdcp.c_str());
    return hdcp == "enable";
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
        case HWC3::Error::SeamlessNotPossible:
            return "SeamlessNotPossible";
    }
}

bool customizeGUIResolution(uint32_t &width, uint32_t &height, uint32_t *uiType) {
    bool ret = true;
    uint32_t w = 0, h = 0, temp = 0;

    char value[PROPERTY_VALUE_MAX];
    char w_buf[PROPERTY_VALUE_MAX];
    char h_buf[PROPERTY_VALUE_MAX];

    memset(value, 0, sizeof(value));
    property_get("ro.boot.gui_resolution", value, "p");
    DEBUG_LOG("%s: sysprop ro.boot.gui_resolution is %s", __FUNCTION__, value);

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
    DEBUG_LOG("%s: get gui resolution: %d x %d, uiType=%d", __FUNCTION__, width, height, *uiType);

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
    ALOGI("%s: displaymode: %d x %d, %d fps, prefered=%d", __FUNCTION__, *width, *height, *vrefresh,
          *prefermode);
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

#ifdef DEBUG_DUMP_REFRESH_RATE
nsecs_t dumpRefreshRateStart() {
    nsecs_t commit_start;
    commit_start = systemTime(CLOCK_MONOTONIC);

    return commit_start;
}

void dumpRefreshRateEnd(uint32_t displayId, int vsyncPeriod, nsecs_t commit_start) {
    static nsecs_t m_pre_commit_start = 0;
    static nsecs_t m_pre_commit_time = 0;
    // surfaceflinger updatescreen delay(compare with vsync period)
    static nsecs_t m_total_sf_delay = 0;
    static nsecs_t m_total_commit_time = 0;
    static nsecs_t m_total_commit_cost = 0;
    static int m_request_refresh_cnt = 0;
    static int m_commit_cnt = 0;

    nsecs_t commit_time;
    float refresh_rate = 0;

    commit_time = systemTime(CLOCK_MONOTONIC);
    char value[PROPERTY_VALUE_MAX];
    property_get("vendor.hwc.debug.dump_refresh_rate", value, "0");
    m_request_refresh_cnt = atoi(value);
    if (m_request_refresh_cnt <= 0)
        return;

    if (m_pre_commit_time > 0) {
        m_total_commit_time += commit_time - m_pre_commit_time;
        m_total_commit_cost += commit_time - commit_start;
        m_total_sf_delay += (int64_t)commit_start - m_pre_commit_start - vsyncPeriod;
        m_commit_cnt++;
        if (m_commit_cnt >= m_request_refresh_cnt) {
            refresh_rate = 1000000000.0 * m_commit_cnt / m_total_commit_time;
            ALOGI("id= %d, refresh rate= %3.2f fps, commit wait=%1.4fms/frame, update "
                  "delay=%1.4fms/frame",
                  displayId, refresh_rate, m_total_commit_cost / (m_commit_cnt * 1000000.0),
                  m_total_sf_delay / (m_commit_cnt * 1000000.0));
            m_total_sf_delay = 0;
            m_total_commit_time = 0;
            m_total_commit_cost = 0;
            m_commit_cnt = 0;
        }
    }
    m_pre_commit_start = commit_start;
    m_pre_commit_time = commit_time;
}

#endif

#ifdef DEBUG_DUMP_FRAME
static void dump_frame_to_file(char *pbuf, int size, char *filename) {
    int fd = 0;
    int len = 0;
    fd = open(filename, O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        ALOGE("Unable to open file [%s]\n", filename);
    }
    len = write(fd, pbuf, size);
    close(fd);
}

static void dump_frame(char *pbuf, int width, int height, int size) {
    static bool start_dump = false;
    static int prev_request_frame_count = 0;
    static int request_frame_count = 0;
    static int dumpped_count = 0;

    if (!start_dump) {
        char value[PROPERTY_VALUE_MAX];
        property_get("vendor.hwc.enable.dump_frame", value, "0");
        request_frame_count = atoi(value);
        // Previous dump request finished, no more request catched
        if (prev_request_frame_count == request_frame_count)
            return;

        prev_request_frame_count = request_frame_count;
        if (request_frame_count >= 1)
            start_dump = true;
        else
            start_dump = false;
    }

    if ((start_dump) && (request_frame_count >= 1)) {
        ALOGI("Dump %d frame buffer %p, %d x %d, size %d", dumpped_count, pbuf, width, height,
              size);
        if (pbuf != 0) {
            char filename[128];
            memset(filename, 0, 128);
            sprintf(filename, "/data/%s-frame-%d.rgba", "drm-display", dumpped_count);
            dump_frame_to_file(pbuf, size, filename);
            dumpped_count++;
        }
        request_frame_count--;
        if (request_frame_count == 0) {
            start_dump = false;
            property_set("vendor.hwc.enable.dump_frame", "0"); // disable dump when completed
        }
    }
}

void debug_dump_frame(buffer_handle_t handle) {
    gralloc_handle_t buffer = (gralloc_handle_t)handle;
    if (buffer->base == 0) {
        void *vaddr = NULL;
        int usage = buffer->usage | USAGE_SW_READ_OFTEN;
        const ::android::Rect rect{0, 0, buffer->width, buffer->height};
        ::android::status_t err =
                ::android::GraphicBufferMapper::get().lock(const_cast<native_handle_t *>(handle),
                                                           usage, rect, &vaddr);
        if (err) {
            ALOGE("%s: GraphicBufferMapper lock failed!", __FUNCTION__);
            return;
        }

        dump_frame((char *)vaddr, buffer->width, buffer->height, buffer->size);

        err = ::android::GraphicBufferMapper::get().unlock(buffer);
        if (err) {
            ALOGE("%s: GraphicBufferMapper unlock failed!", __FUNCTION__);
            return;
        }
    } else {
        dump_frame((char *)buffer->base, buffer->width, buffer->height, buffer->size);
    }
}
#endif

} // namespace aidl::android::hardware::graphics::composer3::impl
