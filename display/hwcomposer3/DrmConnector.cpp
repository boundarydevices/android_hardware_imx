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

#include "DrmConnector.h"

namespace aidl::android::hardware::graphics::composer3::impl {
namespace {

static constexpr const float kMillimetersPerInch = 25.4;

} // namespace

std::unique_ptr<DrmConnector> DrmConnector::create(::android::base::borrowed_fd drmFd,
                                                   uint32_t connectorId) {
    std::unique_ptr<DrmConnector> connector(new DrmConnector(connectorId));

    if (!LoadDrmProperties(drmFd, connectorId, DRM_MODE_OBJECT_CONNECTOR, GetPropertiesMap(),
                           connector.get())) {
        ALOGE("%s: Failed to load connector properties.", __FUNCTION__);
        return nullptr;
    }

    drmModeConnector* drmConnector = drmModeGetConnector(drmFd.get(), connectorId);
    if (!drmConnector) {
        ALOGE("%s: Failed to load connector.", __FUNCTION__);
        return nullptr;
    }
    drmModeEncoder* drmEncoder = drmModeGetEncoder(drmFd.get(), drmConnector->encoders[0]);
    if (!drmEncoder) {
        ALOGE("%s: drmModeGetEncoder failed for encoder 0x%08x", __FUNCTION__,
              drmConnector->encoders[0]);
        return nullptr;
    }
    connector->mPossibleCrtcsMask = drmEncoder->possible_crtcs;
    drmModeFreeEncoder(drmEncoder);
    drmModeFreeConnector(drmConnector);

    if (!connector->update(drmFd)) {
        return nullptr;
    }

    return connector;
}

bool DrmConnector::update(::android::base::borrowed_fd drmFd) {
    DEBUG_LOG("%s: Checking connection for connector:%" PRIu32, __FUNCTION__, mId);

    drmModeConnector* drmConnector = drmModeGetConnector(drmFd.get(), mId);
    if (!drmConnector) {
        ALOGE("%s: Failed to load connector.", __FUNCTION__);
        return false;
    }

    drmModeConnection conn = drmConnector->connection;
    if (conn == mStatus)
        return true;

    mStatus = drmConnector->connection;

    mModes.clear();
    for (int i = 0; i < drmConnector->count_modes; i++) {
        auto mode = DrmMode::create(drmFd, drmConnector->modes[i]);
        if (!mode) {
            ALOGE("%s: Failed to create mode for connector.", __FUNCTION__);
            return false;
        }

        mModes.push_back(std::move(mode));
    }
    ALOGI("there are %zu modes in connector:%d", mModes.size(), mId);
    mWidthMillimeters = drmConnector->mmWidth;
    mHeightMillimeters = drmConnector->mmHeight;

    drmModeFreeConnector(drmConnector);

    if (mStatus == DRM_MODE_CONNECTED) {
        if (!loadEdid(drmFd)) {
            return false;
        }
    }

    DEBUG_LOG("%s: connector:%" PRIu32 " widthMillimeters:%" PRIu32 " heightMillimeters:%" PRIu32,
              __FUNCTION__, mId, mWidthMillimeters, mHeightMillimeters);

    return true;
}

bool DrmConnector::loadEdid(::android::base::borrowed_fd drmFd) {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

#define EDID_LENGTH 128
    uint8_t default_edid[EDID_LENGTH] = {
            // Basic info of the default edid:
            // Vendor ID: NXP, Product ID: 0, Serial Number: 0, Mfg Week: 1, Mfg Year: 2019
            // EDID Structure Version: 1.3, Monitor Name: NXP Android
            0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x3B, 0x10, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x01, 0x1D, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
            0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
            0x01, 0x01, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x4E,
            0x58, 0x50, 0x20, 0x41, 0x6E, 0x64, 0x72, 0x6F, 0x69, 0x64, 0x0A, 0x0A, 0x00,
            0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1E,
    };

    mWidthMillimeters = 0;
    mHeightMillimeters = 0;

    const uint64_t edidBlobId = mEdidProp.getValue();
    if (edidBlobId == (uint64_t)-1) {
        ALOGW("%s: connector:%" PRIu32 " does not have EDID.", __FUNCTION__, mId);
        return true;
    }

    auto blob = drmModeGetPropertyBlob(drmFd.get(), edidBlobId);
    if (!blob) {
        ALOGE("%s: connector:%" PRIu32 " failed to read EDID blob (%" PRIu64 "): %s", __FUNCTION__,
              mId, edidBlobId, strerror(errno));
        //        return false;
        mEdid = std::vector<uint8_t>(default_edid, default_edid + EDID_LENGTH);
    } else {
        const uint8_t* blobStart = static_cast<uint8_t*>(blob->data);
        mEdid = std::vector<uint8_t>(blobStart, blobStart + blob->length);

        drmModeFreePropertyBlob(blob);
    }

    using byte_view = std::basic_string_view<uint8_t>;

    constexpr size_t kEdidDescriptorOffset = 54;
    constexpr size_t kEdidDescriptorLength = 18;

    byte_view edid(mEdid->data(), mEdid->size());
    edid.remove_prefix(kEdidDescriptorOffset);

    byte_view descriptor(edid.data(), kEdidDescriptorLength);
    if (descriptor[0] == 0 && descriptor[1] == 0) {
        ALOGE("%s: connector:%" PRIu32 " is missing preferred detailed timing descriptor.",
              __FUNCTION__, mId);
        return -1;
    }

    const uint8_t w_mm_lsb = descriptor[12];
    const uint8_t h_mm_lsb = descriptor[13];
    const uint8_t w_and_h_mm_msb = descriptor[14];

    mWidthMillimeters = w_mm_lsb | (w_and_h_mm_msb & 0xf0) << 4;
    mHeightMillimeters = h_mm_lsb | (w_and_h_mm_msb & 0xf) << 8;

    return true;
}

bool DrmConnector::buildConfigs(std::shared_ptr<HalConfig> configs, uint32_t startConfigId) {
    DEBUG_LOG("%s: connector:%" PRIu32, __FUNCTION__, mId);

    if (mModes.empty()) {
        return false;
    }

    uint32_t configId = startConfigId;
    //    std::shared_ptr<HalConfig> cfgs = std::make_shared<HalConfig>();
    //    HalConfig cfg;
    //    configs->emplace(configId++, HalDisplayConfig{
    //    (*cfgs)[configId++] = HalDisplayConfig{
    /*            .width = 1920,
                .height = 1080,
                .dpiX = 160,
                .dpiY = 160,
                .refreshRateHz = 60, // mode.vrefresh
                .blobId = 100,
                .modeType = 0,
                .modeWidth = 1920,
                .modeHeight = 1080,});*/
    for (const auto& mode : mModes) {
        configs->emplace(configId++,
                         HalDisplayConfig{
                                 .width = mode->hdisplay,
                                 .height = mode->vdisplay,
                                 .dpiX = mWidthMillimeters == 0
                                         ? 160
                                         : (static_cast<uint32_t>(
                                                   (static_cast<float>(mode->hdisplay) /
                                                    static_cast<float>(mWidthMillimeters)) *
                                                   kMillimetersPerInch)),
                                 .dpiY = mHeightMillimeters == 0
                                         ? 160
                                         : (static_cast<uint32_t>(
                                                   (static_cast<float>(mode->vdisplay) /
                                                    static_cast<float>(mHeightMillimeters)) *
                                                   kMillimetersPerInch)),
                                 .refreshRateHz = (uint32_t)(1000.0f * mode->clock /
                                                                     ((float)mode->vtotal *
                                                                      (float)mode->htotal) +
                                                             0.5f), // mode.vrefresh
                                 .blobId = mode->getBlobId(),
                                 .modeType = mode->type,
                                 .modeWidth = mode->hdisplay,
                                 .modeHeight = mode->vdisplay,
                         });
    }

    return true;
}

uint32_t DrmConnector::getWidth() const {
    DEBUG_LOG("%s: connector:%" PRIu32, __FUNCTION__, mId);

    if (mModes.empty()) {
        return 0;
    }
    return mModes[0]->hdisplay;
}

uint32_t DrmConnector::getHeight() const {
    DEBUG_LOG("%s: connector:%" PRIu32, __FUNCTION__, mId);

    if (mModes.empty()) {
        return 0;
    }
    return mModes[0]->vdisplay;
}

int32_t DrmConnector::getDpiX() const {
    DEBUG_LOG("%s: connector:%" PRIu32, __FUNCTION__, mId);

    if (mModes.empty()) {
        return -1;
    }

    const auto& mode = mModes[0];
    if (mWidthMillimeters) {
        const int32_t dpi = static_cast<int32_t>(
                (static_cast<float>(mode->hdisplay) / static_cast<float>(mWidthMillimeters)) *
                kMillimetersPerInch);
        DEBUG_LOG("%s: connector:%" PRIu32 " has dpi-x:%" PRId32, __FUNCTION__, mId, dpi);
        return dpi;
    }

    return -1;
}

int32_t DrmConnector::getDpiY() const {
    DEBUG_LOG("%s: connector:%" PRIu32, __FUNCTION__, mId);

    if (mModes.empty()) {
        return -1;
    }

    const auto& mode = mModes[0];
    if (mHeightMillimeters) {
        const int32_t dpi = static_cast<int32_t>(
                (static_cast<float>(mode->vdisplay) / static_cast<float>(mHeightMillimeters)) *
                kMillimetersPerInch);
        DEBUG_LOG("%s: connector:%" PRIu32 " has dpi-x:%" PRId32, __FUNCTION__, mId, dpi);
        return dpi;
    }

    return -1;
}

float DrmConnector::getRefreshRate() const {
    DEBUG_LOG("%s: connector:%" PRIu32, __FUNCTION__, mId);

    if (!mModes.empty()) {
        const auto& mode = mModes[0];
        return 1000.0f * mode->clock / ((float)mode->vtotal * (float)mode->htotal);
    }

    return -1.0f;
}

bool DrmConnector::setPowerMode(::android::base::borrowed_fd drmFd, DrmPower power) const {
    DEBUG_LOG("%s: connector:%" PRIu32, __FUNCTION__, mId);

    int mode, err;
    switch (power) {
        case DrmPower::kPowerOff:
            mode = DRM_MODE_DPMS_OFF;
            break;
        case DrmPower::kPowerOn:
            mode = DRM_MODE_DPMS_ON;
            break;
        default:
            mode = DRM_MODE_DPMS_ON;
            break;
    }

    err = drmModeConnectorSetProperty(drmFd.get(), mId, mDpms.getId(), mode);
    if (err != 0) {
        ALOGE("failed to set DPMS mode:%d", mode);
    }

    return err == 0 ? true : false;
}

} // namespace aidl::android::hardware::graphics::composer3::impl
