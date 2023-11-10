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

#include "DrmPlane.h"

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <sstream>
#include <string>

#include "Drm.h"

namespace aidl::android::hardware::graphics::composer3::impl {

std::unique_ptr<DrmPlane> DrmPlane::create(::android::base::borrowed_fd drmFd, uint32_t planeId) {
    std::unique_ptr<DrmPlane> plane(new DrmPlane(planeId));

    DEBUG_LOG("%s: Loading properties for DRM plane:%" PRIu32, __FUNCTION__, planeId);
    if (!LoadDrmProperties(drmFd, planeId, DRM_MODE_OBJECT_PLANE, GetPropertiesMap(),
                           plane.get())) {
        ALOGE("%s: Failed to load plane properties.", __FUNCTION__);
        return nullptr;
    }

    drmModePlanePtr drmPlane = drmModeGetPlane(drmFd.get(), planeId);
    plane->mPossibleCrtcsMask = drmPlane->possible_crtcs;
    for (uint32_t i = 0; i < drmPlane->count_formats; i++) {
        plane->mDrmFormats.push_back(drmPlane->formats[i]);
    }
    drmModeFreePlane(drmPlane);

    const uint64_t inFormatsBlobId = plane->mInFormats.getValue();
    if (inFormatsBlobId == (uint64_t)-1) {
        ALOGW("%s: plane:%" PRIu32 " does not have IN_FORMATS property.", __FUNCTION__, plane->mId);
        return plane;
    }

    uint32_t fmt = 0;
    std::vector<uint64_t> mods;
    drmModePropertyBlobPtr blob;
    drmModeFormatModifierIterator iter = {0};
    blob = drmModeGetPropertyBlob(drmFd.get(), inFormatsBlobId);
    if (!blob) {
        ALOGW("%s: plane:%" PRIu32 " cannot get blob of IN_FORMATS property.", __FUNCTION__,
              plane->mId);
        return plane;
    }
    while (drmModeFormatModifierBlobIterNext(blob, &iter)) {
        if (!fmt || fmt != iter.fmt) {
            plane->mFormatModifiers.emplace(fmt, std::move(mods));
            fmt = iter.fmt;
        }
        mods.push_back(iter.mod);
    }
    drmModeFreePropertyBlob(blob);
#if 0
    char formats_str[2048] = {0};
    char temp[6];
    memset(formats_str, 0, sizeof(formats_str));
    sprintf(formats_str, "in_formats for plane:%d\n", planeId);
    for (auto& pair : plane->mFormatModifiers) {
        char *name = drmGetFormatName(pair.first, temp);
        strcat(formats_str, name);
        strcat(formats_str, ":   ");
        for (auto& mod : pair.second) {
            char *modifier_name = drmGetFormatModifierName(mod);
            strcat(formats_str, modifier_name);
            strcat(formats_str, " ");
            free(modifier_name);
        }
        strcat(formats_str, "\n");
    }
    DEBUG_LOG("%s", formats_str);
#endif
    return plane;
}

bool DrmPlane::isPrimary() const {
    return mType.getValue() == DRM_PLANE_TYPE_PRIMARY;
}

bool DrmPlane::isOverlay() const {
    return mType.getValue() == DRM_PLANE_TYPE_OVERLAY;
}

bool DrmPlane::isCompatibleWith(const DrmCrtc& crtc) {
    return ((0x1 << crtc.mIndexInResourcesArray) & mPossibleCrtcsMask);
}

bool DrmPlane::checkFormat(uint32_t format, uint64_t modifier) {
    for (const auto& pair : mFormatModifiers) {
        if (pair.first == format) {
            auto modIt = std::find(pair.second.begin(), pair.second.end(), modifier);
            if (modIt != pair.second.end())
                return true;
        }
    }
    return false;
}

bool DrmPlane::checkFormatSupported(uint32_t format) {
    bool supported = std::any_of(mDrmFormats.begin(), mDrmFormats.end(),
                                 [&](uint32_t fmt) { return fmt == format; });

    return supported;
}

} // namespace aidl::android::hardware::graphics::composer3::impl
