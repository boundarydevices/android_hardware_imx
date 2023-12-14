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

#include "DrmCrtc.h"

namespace aidl::android::hardware::graphics::composer3::impl {

std::unique_ptr<DrmCrtc> DrmCrtc::create(::android::base::borrowed_fd drmFd, uint32_t crtcId,
                                         uint32_t crtcIndexInResourcesArray) {
    std::unique_ptr<DrmCrtc> crtc(new DrmCrtc(crtcId, crtcIndexInResourcesArray));

    DEBUG_LOG("%s: Loading properties for crtc:%" PRIu32, __FUNCTION__, crtcId);
    if (!LoadDrmProperties(drmFd, crtcId, DRM_MODE_OBJECT_CRTC, GetPropertiesMap(), crtc.get())) {
        ALOGE("%s: Failed to load crtc properties.", __FUNCTION__);
        return nullptr;
    }

    return crtc;
}

bool DrmCrtc::setLowPowerDisplay(::android::base::borrowed_fd drmFd, DrmPower power) const {
    DEBUG_LOG("%s: crtc:%" PRIu32, __FUNCTION__, mId);

    int mode, err;
    switch (power) {
        case DrmPower::kPowerOff:
            mode = 2; // TODO: need more accurate definition
            break;
        case DrmPower::kPowerOn:
            mode = 1; // TODO: need more accurate definition
            break;
        default:
            mode = 1;
            break;
    }

    err = drmModeObjectSetProperty(drmFd.get(), mId, DRM_MODE_OBJECT_CRTC, mDisplayXfer.getId(),
                                   mode);
    if (err != 0) {
        ALOGE("failed to set low power display mode:%d", mode);
    }

    return err == 0 ? true : false;
}

} // namespace aidl::android::hardware::graphics::composer3::impl
