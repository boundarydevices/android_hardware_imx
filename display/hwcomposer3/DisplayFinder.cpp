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

#include "DisplayFinder.h"

#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/strings.h>

#include <map>

#include "Common.h"

namespace aidl::android::hardware::graphics::composer3::impl {
namespace {

static int getVsyncHzFromProperty() {
    static constexpr const auto kVsyncProp = "ro.boot.qemu.vsync";

    const auto vsyncProp = ::android::base::GetProperty(kVsyncProp, "");
    DEBUG_LOG("%s: prop value is: %s", __FUNCTION__, vsyncProp.c_str());

    uint64_t vsyncPeriod;
    if (!::android::base::ParseUint(vsyncProp, &vsyncPeriod)) {
        ALOGE("%s: failed to parse vsync period '%s', returning default 60", __FUNCTION__,
              vsyncProp.c_str());
        return 60;
    }

    return static_cast<int>(vsyncPeriod);
}

// This is currently only used for Gem5 bring-up where virtio-gpu and drm
// are not currently available. For now, just return a placeholder display.
HWC3::Error findNoOpDisplays(std::vector<DisplayMultiConfigs>* outDisplays) {
    outDisplays->push_back(DisplayMultiConfigs{
            .displayId = 0,
            .activeConfigId = 0,
            .configs = {DisplayConfig(0,
                                      /*width=*/720,                         //
                                      /*heighth=*/1280,                      //
                                      /*dpiXh=*/320,                         //
                                      /*dpiYh=*/320,                         //
                                      /*vsyncPeriod=*/HertzToPeriodNanos(30) //
                                      )},
    });

    return HWC3::Error::None;
}

HWC3::Error findDrmDisplays(const DrmClient* drm, std::vector<DisplayMultiConfigs>* outDisplays) {
    std::vector<HalMultiConfigs> drmDisplayConfigs;

    HWC3::Error error = drm->getDisplayConfigs(&drmDisplayConfigs);
    if (error != HWC3::Error::None) {
        ALOGE("%s: failed to get displays configs.", __FUNCTION__);
        return error;
    }

    for (const HalMultiConfigs drmDisplayConfig : drmDisplayConfigs) {
        std::vector<DisplayConfig> hwcConfigs;
        for (const auto& pair : *(drmDisplayConfig.configs)) {
            auto cfg = pair.second;
            hwcConfigs.push_back(DisplayConfig(static_cast<int32_t>(pair.first), cfg.width,
                                               cfg.height, cfg.dpiX, cfg.dpiY,
                                               HertzToPeriodNanos(cfg.refreshRateHz)));
        }
        outDisplays->push_back(DisplayMultiConfigs{
                .displayId = drmDisplayConfig.displayId,
                .activeConfigId = static_cast<int32_t>(drmDisplayConfig.activeConfigId),
                .configs = hwcConfigs,
        });
    }

    return HWC3::Error::None;
}

} // namespace

HWC3::Error findDisplays(FrameComposer* composer, std::vector<DisplayMultiConfigs>* outDisplays) {
    HWC3::Error error = HWC3::Error::NoResources;
    std::map<uint32_t, DrmClient*> clients;

    outDisplays->clear();
    composer->getAllDrmClients(clients);

    if (IsInGem5DisplayFinderMode() || IsInNoOpCompositionMode()) {
        error = findNoOpDisplays(outDisplays);
    } else if (IsInDrmDisplayFinderMode()) {
        //    error = findDrmDisplays(*drm, outDisplays);
    } else {
        for (const auto& pair : clients) {
            HWC3::Error err = findDrmDisplays(pair.second, outDisplays);
            if (err == HWC3::Error::None) {
                error = HWC3::Error::None;
            }
        }
    }

    if (error != HWC3::Error::None) {
        // assert(clients.size() > 0)
        ALOGW("%s: No display connected, use placeholder for primary display", __FUNCTION__);
        uint32_t minId = INT_MAX;
        for (const auto& [baseId, _] :
             clients) { // imx8mq has two DRM clients, imx-dcss and mxsfb-drm
            if (baseId < minId)
                minId = baseId;
        }
        auto id = clients[minId]->getDisplayBaseId();
        clients[minId]->setPrimaryDisplay(id); // select minimum display id as primary
        clients[minId]->fakeDisplayConfig(id); // generate a fake display config

        HWC3::Error err = findDrmDisplays(clients[minId], outDisplays); // try again
        if (err != HWC3::Error::None) {
            return err; // Should not happen!
        }
    } else {
        uint32_t minDisplayId = INT_MAX;
        uint32_t minBaseId = INT_MAX;
        for (const auto& disp : *outDisplays) {
            if (disp.displayId < minDisplayId)
                minDisplayId = disp.displayId;
        }
        for (const auto& [baseId, _] : clients) {
            if (baseId <= minDisplayId)
                minBaseId = baseId;
            if (baseId > minDisplayId)
                break;
        }

        clients[minBaseId]->setPrimaryDisplay(minDisplayId);
        ALOGI("%s: %zu displays connected, select id=%d as primary display", __FUNCTION__,
              outDisplays->size(), minDisplayId);
    }

    for (auto& display : *outDisplays) {
        DisplayConfig::addConfigGroups(&display.configs);
    }

    return HWC3::Error::None;
}

} // namespace aidl::android::hardware::graphics::composer3::impl
