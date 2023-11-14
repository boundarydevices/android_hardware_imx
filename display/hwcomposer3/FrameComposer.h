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

#ifndef ANDROID_HWC_COMPOSER_H
#define ANDROID_HWC_COMPOSER_H

#include <android-base/unique_fd.h>

#include <functional>
#include <map>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "Common.h"
#include "DeviceClient.h"
#include "DisplayChanges.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class Display;

class FrameComposer {
public:
    virtual ~FrameComposer() {}

    virtual HWC3::Error init() = 0;

    using HotplugCallback = std::function<void(bool /*connected*/, //
                                               std::unique_ptr<HalMultiConfigs> /*configs*/)>;

    virtual HWC3::Error registerOnHotplugCallback(const HotplugCallback& cb) = 0;

    virtual HWC3::Error unregisterOnHotplugCallback() = 0;

    virtual HWC3::Error onDisplayCreate(Display* display) = 0;

    virtual HWC3::Error onDisplayDestroy(Display* display) = 0;

    virtual HWC3::Error onDisplayLayerDestroy(Display* display, Layer* layer) = 0;

    virtual HWC3::Error onDisplayClientTargetSet(Display* display) = 0;

    // Determines if this composer can compose the given layers and requests
    // changes for layers that can't not be composed.
    virtual HWC3::Error validateDisplay(Display* display, DisplayChanges* outChanges) = 0;

    // Performs the actual composition of layers and presents the composed result
    // to the display.
    virtual HWC3::Error presentDisplay(
            Display* display, ::android::base::unique_fd* outDisplayFence,
            std::unordered_map<int64_t, ::android::base::unique_fd>* outLayerFences) = 0;

    virtual HWC3::Error onActiveConfigChange(Display* display) = 0;
    virtual HWC3::Error setPowerMode(Display* display, PowerMode mode) = 0;
    virtual HWC3::Error setDisplayBrightness(Display* display, float brightness) = 0;
    virtual HWC3::Error getDisplayConnectionType(Display* display,
                                                 DisplayConnectionType* outType) = 0;

    /*  virtual const DrmClient* getDrmPresenter() const {
        return nullptr;
      }*/
    virtual HWC3::Error getAllDeviceClients(std::map<uint32_t, DeviceClient*>& clients) = 0;
};

} // namespace aidl::android::hardware::graphics::composer3::impl

#endif
