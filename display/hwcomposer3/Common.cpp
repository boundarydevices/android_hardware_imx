/*
 * Copyright 2022 The Android Open Source Project
 * Copyright 2023-2024 NXP
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
    const std::string disable = ::android::base::GetProperty("vendor.sys.hwc.disable", "0");
    DEBUG_LOG("%s: sysprop vendor.sys.hwc.disable is %s", __FUNCTION__, disable.c_str());
    const std::string dpu = ::android::base::GetProperty("ro.boot.dpu_composition", "");
    DEBUG_LOG("%s: sysprop ro.boot.dpu_composition is %s", __FUNCTION__, dpu.c_str());
    return (disable == "1") || (dpu == "0");
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

std::string getHdcpStatusPath() {
    std::string status_path = ::android::base::GetProperty("vendor.hdcp_status_path", "");
    DEBUG_LOG("%s: property hdcp_status_path is %s", __FUNCTION__, status_path.c_str());
    return status_path;
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
        w = static_cast<uint32_t>(std::stoul(w_buf));
        h = static_cast<uint32_t>(std::stoul(h_buf));
        *uiType = UI_SCALE_HARDWARE;
    } else if (!strncmp(value, "ssw", 3) &&
               (sscanf(value, "ssw%[0-9]x%[0-9]", w_buf, h_buf) == 2)) {
        w = static_cast<uint32_t>(std::stoul(w_buf));
        h = static_cast<uint32_t>(std::stoul(h_buf));
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
    uint32_t width;
    uint32_t height;
    uint32_t vrefresh;
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
        uint32_t modeResult[3] = {0};
        // displaymode format should be 1920x1080p60
        if (strstr(value, "x") && strstr(value, "p")) {
            char *s = strdup(value);
            char *token;
            for (i = 0, token = strsep(&s, delim); i < 3 && token != NULL;
                 token = strsep(&s, delim), i++) {
                modeResult[i] = static_cast<uint32_t>(std::stoul(token));
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

void dumpRefreshRateEnd(DumpRefreshRate &dump, uint32_t vsyncPeriod, nsecs_t commit_start) {
    nsecs_t commit_time;
    double refresh_rate = 0;

    commit_time = systemTime(CLOCK_MONOTONIC);
    char value[PROPERTY_VALUE_MAX];
    property_get("vendor.hwc.debug.dump_refresh_rate", value, "0");
    dump.request_refresh_cnt = atoi(value);
    if (dump.request_refresh_cnt <= 0)
        return;

    if (dump.pre_commit_time > 0) {
        dump.total_commit_time += commit_time - dump.pre_commit_time;
        dump.total_commit_cost += commit_time - commit_start;
        dump.total_sf_delay += (int64_t)commit_start - dump.pre_commit_start - vsyncPeriod;
        dump.commit_cnt++;
        if (dump.commit_cnt >= dump.request_refresh_cnt) {
            refresh_rate = 1000000000.0 * dump.commit_cnt / dump.total_commit_time;
            ALOGI("id= %d, refresh rate= %3.2f fps, commit wait=%1.4fms/frame, update "
                  "delay=%1.4fms/frame",
                  dump.displayId, refresh_rate,
                  dump.total_commit_cost / (dump.commit_cnt * 1000000.0),
                  dump.total_sf_delay / (dump.commit_cnt * 1000000.0));
            dump.total_sf_delay = 0;
            dump.total_commit_time = 0;
            dump.total_commit_cost = 0;
            dump.commit_cnt = 0;
        }
    }
    dump.pre_commit_start = commit_start;
    dump.pre_commit_time = commit_time;
}

#endif

#ifdef DEBUG_DUMP_FRAME
static void dump_frame_to_file(void *pbuf, int size, char *filename) {
    FILE *fp = fopen(filename, "wb");
    if (fp) {
        size_t result = fwrite(pbuf, size, 1, fp);
        ALOGI("Success to write %zu data, size(%d) to %s", result, size, filename);
        fclose(fp);
    } else {
        ALOGE("Unable to open file [%s]\n", filename);
    }
}

static void dump_frame(buffer_handle_t handle, std::string prefix, uint32_t count, uint32_t index) {
    HandleInfo info;
    if (handle == nullptr || (getInfoFromHandle(handle, &info) != 0)) {
        ALOGE("%s: invalid native handle", __FUNCTION__);
        return;
    }

    char filename[128];
    memset(filename, 0, 128);
    char *sequence = (char *)&(info.drm_format);
    std::string s(sequence, 4);
    sprintf(filename, "/data/vendor/hwc/%s-%d-%s-%dx%d-%d.dat", prefix.c_str(), count, s.c_str(),
            info.stride, info.height, index);

    if (info.base == 0) {
        void *vaddr = nullptr;
        int usage = info.usage | GRALLOC_USAGE_SW_READ_OFTEN;
        const ::android::Rect rect{0, 0, static_cast<int32_t>(info.width),
                                   static_cast<int32_t>(info.height)};
        ::android::status_t err =
                ::android::GraphicBufferMapper::get().lock(const_cast<native_handle_t *>(handle),
                                                           usage, rect, &vaddr);
        if (err || vaddr == nullptr) {
            ALOGE("%s: GraphicBufferMapper lock failed!", __FUNCTION__);
            return;
        }

        dump_frame_to_file((char *)vaddr, info.size, filename);

        err = ::android::GraphicBufferMapper::get().unlock(handle);
        if (err) {
            ALOGE("%s: GraphicBufferMapper unlock failed!", __FUNCTION__);
            return;
        }
    } else {
        dump_frame_to_file((char *)info.base, info.size, filename);
    }
}

static bool start_dump = false;
static bool dump_frame_last = false;
static int dumpped_count = 0;
void debug_dump_framebuffer(buffer_handle_t handle) {
    static int prev_request_frame_count = 0;
    static int request_frame_count = 0;

    if (!start_dump) {
#ifdef DEBUG_DUMP_LAYER_BUFFER
        dump_frame_last = false;
#endif
        char value[PROPERTY_VALUE_MAX];
        property_get("vendor.hwc.debug.dump_frame", value, "0");
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
        dump_frame(handle, "fb", ++dumpped_count, 0);

        request_frame_count--;
        if (request_frame_count == 0) {
            start_dump = false;
#ifdef DEBUG_DUMP_LAYER_BUFFER
            dump_frame_last = true;
#endif
            property_set("vendor.hwc.debug.dump_frame", "0"); // disable dump when completed
        }
    }
}
#ifdef DEBUG_DUMP_LAYER_BUFFER
void debug_dump_layerbuffer(buffer_handle_t handle, uint32_t index) {
    if (start_dump || dump_frame_last) {
        dump_frame(handle, "layer", dumpped_count, index);
    }
}
#endif
#endif

bool getDisplayPortFromProperty(const std::string &connector_name, uint32_t *outPort) {
    const std::string ports = ::android::base::GetProperty("ro.boot.display_port", "");
    DEBUG_LOG("%s: sysprop ro.boot.display_port is %s", __FUNCTION__, ports.c_str());

    uint32_t port;
    if (ports.size() > 0) {
        std::string conn = connector_name + ":";
        auto pos = ports.find(conn);
        if (pos != std::string::npos) {
            auto colon = ports.find(':', pos);
            if (colon == std::string::npos)
                return false;
            auto comma = ports.find(',', colon);
            auto count = (comma == std::string::npos) ? comma : (comma - colon - 1);
            auto port_str = ports.substr(colon + 1, count);
            if (!std::all_of(port_str.begin(), port_str.end(), ::isdigit))
                return false;

            port = static_cast<uint32_t>(std::stoul(port_str));
            *outPort = port;
            return true;
        }
    }
    return false;
}

} // namespace aidl::android::hardware::graphics::composer3::impl
