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

#include "DrmDisplay.h"

#include <gralloc_handle.h>
#include <stdlib.h>
#include <xf86drm.h>

#include "Common.h"
#include "Drm.h"
#include "DrmAtomicRequest.h"

namespace aidl::android::hardware::graphics::composer3::impl {
namespace {

template <typename T>
uint64_t addressAsUint(T* pointer) {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer));
}

} // namespace

std::unique_ptr<DrmDisplay> DrmDisplay::create(
        uint32_t id, std::unique_ptr<DrmConnector> connector, std::unique_ptr<DrmCrtc> crtc,
        std::unordered_map<uint32_t, std::unique_ptr<DrmPlane>>& planes,
        ::android::base::borrowed_fd drmFd) {
    if (!crtc) {
        ALOGE("%s: invalid crtc.", __FUNCTION__);
        return nullptr;
    }
    if (!connector) {
        ALOGE("%s: invalid connector.", __FUNCTION__);
        return nullptr;
    }
    if (planes.size() == 0) {
        ALOGE("%s: invalid plane.", __FUNCTION__);
        return nullptr;
    }
    char planeStr[100] = {0}, tempStr[100];
    for (const auto& [planeId, _] : planes) {
        sprintf(tempStr, "%d ", planeId);
        strcat(planeStr, tempStr);
    }

    ALOGI("%s: display %d created: crtc=%d, connector=%d, plane=%s", __FUNCTION__, id,
          crtc->getId(), connector->getId(), planeStr);

    std::unique_ptr<DrmDisplay> display(
            new DrmDisplay(id, std::move(connector), std::move(crtc), std::move(planes)));

    return std::move(display);
}

std::tuple<HWC3::Error, std::unique_ptr<DrmAtomicRequest>> DrmDisplay::flushOverlay(
        uint32_t planeId, std::unique_ptr<DrmAtomicRequest> request,
        const std::shared_ptr<DrmBuffer>& buffer) {
    if (mPlanes.find(planeId) == mPlanes.end()) {
        ALOGE("%s: Not find the plane:%d to flush", __FUNCTION__, planeId);
        return std::make_tuple(HWC3::Error::BadParameter, std::move(request));
    }

    if (request.get() == nullptr) {
        request = DrmAtomicRequest::create();
        if (!request) {
            ALOGE("%s: failed to create atomic request.", __FUNCTION__);
            return std::make_tuple(HWC3::Error::NoResources, nullptr);
        }
    }

    HalDisplayConfig config = (*mConfigs)[mActiveConfigId];
    common::Rect& rectF = buffer->mDisplayFrame;
    common::Rect& rectS = buffer->mSourceCrop;
    int x0 = rectF.left * config.modeWidth / config.width;
    int y0 = rectF.top * config.modeHeight / config.height;
    int wF = (rectF.right - rectF.left) * config.modeWidth / config.width;
    int hF = (rectF.bottom - rectF.top) * config.modeHeight / config.height;
    int wS = rectS.right - rectS.left;
    int hS = rectS.bottom - rectS.top;
    // alignment is needed for imx8mq
    wF = ALIGN_PIXEL_2(wF - 1);
    hF = ALIGN_PIXEL_2(hF - 1);
    wS = ALIGN_PIXEL_2(wS - 1);
    hS = ALIGN_PIXEL_2(hS - 1);

    DrmPlane* plane = mPlanes[planeId].get();
    bool okay = true;
    okay &= request->Set(planeId, plane->getCrtcProperty(), mCrtc->getId());
    okay &= request->Set(planeId, plane->getFbProperty(), *buffer->mDrmFramebuffer);
    okay &= request->Set(planeId, plane->getCrtcXProperty(), x0);
    okay &= request->Set(planeId, plane->getCrtcYProperty(), y0);
    okay &= request->Set(planeId, plane->getCrtcWProperty(), wF);
    okay &= request->Set(planeId, plane->getCrtcHProperty(), hF);
    okay &= request->Set(planeId, plane->getSrcXProperty(), rectS.left);
    okay &= request->Set(planeId, plane->getSrcYProperty(), rectS.top);
    okay &= request->Set(planeId, plane->getSrcWProperty(), wS << 16);
    okay &= request->Set(planeId, plane->getSrcHProperty(), hS << 16);

    //    auto prop = mPlanes[planeId]->getDtrcTableOffestProperty();
    //    auto meta = buffer->mMeta;
    //    if ((prop.getValue() != -1) && (meta != NULL) && (meta->mFlags & FLAGS_COMPRESSED_OFFSET))
    //    {
    //        okay &= request->Set(planeId, prop, meta->mYOffset | uint64_t(meta->mUVOffset) << 32);
    //        meta->mFlags &= ~FLAGS_COMPRESSED_OFFSET;
    //    }

    DEBUG_LOG("%s: crtc:x0=%d, y0=%d, wd=%d, hd=%d, src:x0=%d, y0=%d, ws=%d, hs=%d", __func__, x0,
              y0, wF, hF, rectS.left, rectS.top, wS, hS);
    if (!okay) {
        ALOGE("%s: failed to flush to Overlay plane:%d.", __FUNCTION__, planeId);
        return std::make_tuple(HWC3::Error::NoResources, std::move(request));
    }

    mTempBuffers.planeDrmBuffer[planeId] = buffer;

    plane->setActive(true);
    DEBUG_LOG("%s: flush overlay plane:%d, fbId=%d", __FUNCTION__, planeId,
              *buffer->mDrmFramebuffer);
    return std::make_tuple(HWC3::Error::None, std::move(request));
}

std::tuple<HWC3::Error, std::unique_ptr<DrmAtomicRequest>> DrmDisplay::flushPrimary(
        uint32_t planeId, std::unique_ptr<DrmAtomicRequest> request,
        ::android::base::borrowed_fd inSyncFd, const std::shared_ptr<DrmBuffer>& buffer) {
    if (mPlanes.find(planeId) == mPlanes.end()) {
        ALOGE("%s: Not find the plane:%d to flush", __FUNCTION__, planeId);
        return std::make_tuple(HWC3::Error::BadParameter, std::move(request));
    }

    if (request.get() == nullptr) {
        request = DrmAtomicRequest::create();
        if (!request) {
            ALOGE("%s: failed to create atomic request.", __FUNCTION__);
            return std::make_tuple(HWC3::Error::NoResources, nullptr);
        }
    }

    HalDisplayConfig config = (*mConfigs)[mActiveConfigId];
    int sh, sw, dh, dw;
    if (mUiScaleType == UI_SCALE_SOFTWARE) {
        sw = config.modeWidth;
        sh = config.modeHeight;
        dw = config.modeWidth;
        dh = config.modeHeight;
    } else if (mUiScaleType == UI_SCALE_HARDWARE) {
        sw = config.width;
        sh = config.height;
        dw = config.width;
        dh = config.height;
    } else {
        sw = config.width;
        sh = config.height;
        dw = config.modeWidth;
        dh = config.modeHeight;
    }

    DrmPlane* plane = mPlanes[planeId].get();
    bool okay = true;
    okay &= request->Set(planeId, plane->getCrtcProperty(), mCrtc->getId());
    okay &= request->Set(planeId, plane->getInFenceProperty(), inSyncFd.get());
    okay &= request->Set(planeId, plane->getFbProperty(), *buffer->mDrmFramebuffer);
    okay &= request->Set(planeId, plane->getCrtcXProperty(), 0);
    okay &= request->Set(planeId, plane->getCrtcYProperty(), 0);
    okay &= request->Set(planeId, plane->getCrtcWProperty(), dw);
    okay &= request->Set(planeId, plane->getCrtcHProperty(), dh);
    okay &= request->Set(planeId, plane->getSrcXProperty(), 0);
    okay &= request->Set(planeId, plane->getSrcYProperty(), 0);
    okay &= request->Set(planeId, plane->getSrcWProperty(), sw << 16);
    okay &= request->Set(planeId, plane->getSrcHProperty(), sh << 16);

    if (!okay) {
        ALOGE("%s: failed to flush Primary plane:%d.", __FUNCTION__, planeId);
        return std::make_tuple(HWC3::Error::NoResources, std::move(request));
    }

    mTempBuffers.clientTargetDrmBuffer = buffer;

    plane->setActive(true);
    DEBUG_LOG("%s: flush primary plane:%d, fbId=%d", __FUNCTION__, planeId,
              *buffer->mDrmFramebuffer);
    return std::make_tuple(HWC3::Error::None, std::move(request));
}

std::tuple<HWC3::Error, ::android::base::unique_fd> DrmDisplay::commit(
        std::unique_ptr<DrmAtomicRequest> request, ::android::base::borrowed_fd drmFd) {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    if (request.get() == nullptr) {
        request = DrmAtomicRequest::create();
        if (!request) {
            ALOGE("%s: failed to create atomic request.", __FUNCTION__);
            return std::make_tuple(HWC3::Error::NoResources, ::android::base::unique_fd());
        }
    }

    char activeStr[100] = {0}, disableStr[100] = {0}, tempStr[10];
    bool okay = true;
    for (const auto& pair : mPlanes) {
        DrmPlane* plane = pair.second.get();
        if (plane->checkActive()) {
            sprintf(tempStr, "%d ", pair.first);
            strcat(activeStr, tempStr);
            continue;
        }
        sprintf(tempStr, "%d ", pair.first);
        strcat(disableStr, tempStr);

        okay &= request->Set(plane->getId(), plane->getCrtcProperty(), 0);
        okay &= request->Set(plane->getId(), plane->getFbProperty(), 0);
    }

    int flushFenceFd = -1;

    if (mModeSet) {
        uint32_t modeBlobId = INT_MAX;
        if (mActiveConfigId >= 0) {
            modeBlobId = mActiveConfig.blobId;
        } else {
            modeBlobId = mConnector->getDefaultMode()->getBlobId();
        }
        okay &= request->Set(mConnector->getId(), mConnector->getCrtcProperty(), mCrtc->getId());
        okay &= request->Set(mCrtc->getId(), mCrtc->getActiveProperty(), 1);
        okay &= request->Set(mCrtc->getId(), mCrtc->getModeProperty(), modeBlobId);
        ALOGI("%s: do mode set for display:%d", __FUNCTION__, mId);
    }
    okay &= request->Set(mCrtc->getId(), mCrtc->getOutFenceProperty(),
                         addressAsUint(&flushFenceFd));
    okay &= request->Commit(drmFd);

    if (!okay) {
        ALOGE("%s: failed to commit.", __FUNCTION__);
        return std::make_tuple(HWC3::Error::NoResources, ::android::base::unique_fd());
    }

    if (mModeSet)
        mModeSet = false;

    for (auto& pair : mPlanes) {
        DrmPlane* plane = pair.second.get();
        plane->setActive(false);
    }
    mPreviousBuffers.clientTargetDrmBuffer = mTempBuffers.clientTargetDrmBuffer;
    mPreviousBuffers.planeDrmBuffer = mTempBuffers.planeDrmBuffer;

    DEBUG_LOG("%s: atomic commit display:%d, plane:active=%s,disabled=%s; present fence:%d\n",
              __FUNCTION__, mId, activeStr, disableStr, flushFenceFd);
    return std::make_tuple(HWC3::Error::None, ::android::base::unique_fd(flushFenceFd));
}

bool DrmDisplay::onConnect(::android::base::borrowed_fd drmFd) {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    updateDisplayConfigs();

    mModeSet = true;
    return true;
}

bool DrmDisplay::onDisconnect(::android::base::borrowed_fd drmFd) {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    mPreviousBuffers.clientTargetDrmBuffer = nullptr;
    mPreviousBuffers.planeDrmBuffer.clear();
    if (!isPrimary()) {
        // primary display cannot be disconnected, fake display config is generated according
        // to current active config
        mActiveConfigId = -1;
        mConfigs->clear();
    }

    return true; // okay;
}

DrmHotplugChange DrmDisplay::checkAndHandleHotplug(::android::base::borrowed_fd drmFd) {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    const bool oldConnected = mConnector->isConnected();
    mConnector->update(drmFd);
    const bool newConnected = mConnector->isConnected();

    if (oldConnected == newConnected) {
        return DrmHotplugChange::kNoChange;
    }

    if (newConnected) {
        ALOGI("%s: display:%" PRIu32 " was connected.", __FUNCTION__, mId);
        if (!onConnect(drmFd)) {
            ALOGE("%s: display:%" PRIu32 " failed to connect.", __FUNCTION__, mId);
        }
        return DrmHotplugChange::kConnected;
    } else {
        ALOGI("%s: display:%" PRIu32 " was disconnected.", __FUNCTION__, mId);
        if (!onDisconnect(drmFd)) {
            ALOGE("%s: display:%" PRIu32 " failed to disconnect.", __FUNCTION__, mId);
        }
        return DrmHotplugChange::kDisconnected;
    }
}

bool DrmDisplay::setPowerMode(::android::base::borrowed_fd drmFd, DrmPower power) {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    mConnector->setPowerMode(drmFd, power);

    return true;
}

void DrmDisplay::buildPlaneIdPool() {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    mPlaneIdPool.clear();
    for (const auto& pair : mPlanes) {
        DEBUG_LOG("check plane %d type: %d", pair.second->getId(),
                  pair.second->isOverlay() ? 0 : 1);
        if (pair.second->isOverlay())
            mPlaneIdPool.push_back(pair.first);
    }

    DEBUG_LOG("%s: display:%" PRIu32 " there are %zu overlay plane", __FUNCTION__, mId,
              mPlaneIdPool.size());
}

uint32_t DrmDisplay::findDrmPlane(const native_handle_t* handle) {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    if (mPlaneIdPool.size() == 0) {
        DEBUG_LOG("%s: overlay plane pool is empty", __FUNCTION__);
        return 0;
    }

    gralloc_handle_t memHandle = (gralloc_handle_t)handle;
    if (memHandle == nullptr) {
        ALOGE("%s: display:%" PRIu32 " invalid gralloc_handle", __FUNCTION__, mId);
        return 0;
    }

    uint64_t modifier;
    uint32_t format = ConvertNxpFormatToDrmFormat(memHandle->fslFormat, &modifier);
    if (format == 0) {
        ALOGE("%s: display:%" PRIu32 " unknown format:0x%x", __FUNCTION__, mId,
              memHandle->fslFormat);
        return 0;
    }

    if (memHandle->format_modifier > 0)
        modifier = memHandle->format_modifier;

    uint32_t planeId = 0;
    auto it = std::find_if(mPlaneIdPool.begin(), mPlaneIdPool.end(), [&](uint32_t id) {
        if (mPlanes[id]->checkFormat(format, modifier)) {
            planeId = id;
            return true;
        }
        return false;
    });
    if (it != mPlaneIdPool.end()) {
        mPlaneIdPool.erase(it);
    }
    if (planeId > 0) {
        char fmt[6];
        char* name = drmGetFormatName(format, fmt);
        char* modifier_name = drmGetFormatModifierName(modifier);
        DEBUG_LOG("%s: find suitable Drm Plane:%d for buffer:%s :%s %s", __FUNCTION__, planeId,
                  memHandle->name, name, modifier_name);
    }

    return planeId;
}

uint32_t DrmDisplay::getPrimaryPlaneId() {
    uint32_t planeId = 0;
    for (const auto& pair : mPlanes) {
        if (pair.second->isPrimary())
            planeId = pair.first;
    }

    return planeId;
}

std::shared_ptr<HalConfig> DrmDisplay::getDisplayConfigs() {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    return mConfigs;
}

void DrmDisplay::updateActiveConfig(std::shared_ptr<HalConfig> configs) {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    uint32_t index = 0;
    uint32_t delta = -1, rdelta = -1;
    uint32_t dst_width = 0, dst_height = 0, dst_vrefresh = 60, prefered_mode = 0;
    parseDisplayMode(&dst_width, &dst_height, &dst_vrefresh,
                     &prefered_mode); // display mode set by bootargs.

    for (uint32_t i = 0; i < configs->size(); i++) {
        auto& cfg = (*configs)[mStartConfigId + i];
        if ((prefered_mode == 1) && (cfg.modeType & DRM_MODE_TYPE_PREFERRED)) {
            index = i;
            break;
        }

        rdelta = labs((int)dst_width - (int)cfg.width) + labs((int)dst_height - (int)cfg.height);
        if (rdelta < delta) {
            delta = rdelta;
            index = i;
        } else if (rdelta == delta) {
            auto& prev = (*configs)[index];
            if (labs((int)dst_vrefresh - (int)cfg.refreshRateHz) <
                labs((int)dst_vrefresh - (int)prev.refreshRateHz))
                index = i;
        }
    }

    mActiveConfigId = mStartConfigId + index;
    auto activeConfig = (*configs)[mActiveConfigId];
    ALOGI("Find best mode w:%d, h:%d, refreshrate:%d at mode index %d", activeConfig.width,
          activeConfig.height, activeConfig.refreshRateHz, index);

    uint32_t width = activeConfig.width;
    uint32_t height = activeConfig.height;
    if (customizeGUIResolution(width, height, &mUiScaleType)) {
        HalDisplayConfig newConfig{0};
        newConfig.width = width;
        newConfig.height = height;
        newConfig.dpiX = 160;
        newConfig.dpiY = 160;
        newConfig.refreshRateHz = activeConfig.refreshRateHz;
        newConfig.blobId = activeConfig.blobId;
        newConfig.modeWidth = activeConfig.width;
        newConfig.modeHeight = activeConfig.height;

        configs->emplace(mStartConfigId + configs->size(), newConfig);
        mActiveConfigId = mStartConfigId + configs->size();
    }
    mActiveConfig = (*configs)[mActiveConfigId];

    uint32_t format = FORMAT_RGBA8888;
    ALOGI("Display index= %d \n"
          "configId     = %d \n"
          "xres         = %d px\n"
          "yres         = %d px\n"
          "format       = %d\n"
          "xdpi         = %d ppi\n"
          "ydpi         = %d ppi\n"
          "fps          = %d Hz\n"
          "mode.width   = %d px\n"
          "mode.height  = %d px\n",
          mId, mActiveConfigId, mActiveConfig.width, mActiveConfig.height, format,
          mActiveConfig.dpiX, mActiveConfig.dpiY, mActiveConfig.refreshRateHz,
          mActiveConfig.modeWidth, mActiveConfig.modeHeight);
}

bool DrmDisplay::updateDisplayConfigs() {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    mStartConfigId = mStartConfigId + mConfigs->size();
    mConfigs->clear();
    if (mConnector->buildConfigs(mConfigs, mStartConfigId)) {
        updateActiveConfig(mConfigs);
    }

    return true;
}

void DrmDisplay::placeholderDisplayConfigs() {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    mStartConfigId = mStartConfigId + mConfigs->size();
    mConfigs->clear();

    HalDisplayConfig newConfig{0};
    if (mActiveConfigId >= 0) {
        memcpy(&newConfig, &mActiveConfig, sizeof(HalDisplayConfig));
        newConfig.blobId = 0;
        newConfig.modeWidth = 0;
        newConfig.modeHeight = 0;
    } else {
        newConfig.width = 1080;
        newConfig.height = 1920;
        newConfig.dpiX = 320;
        newConfig.dpiY = 320;
        newConfig.refreshRateHz = 60;
    }

    mConfigs->emplace(mStartConfigId, newConfig);
    mActiveConfigId = mStartConfigId;

    mActiveConfig = (*mConfigs)[mActiveConfigId];
}

int DrmDisplay::createDeviceFramebuffer(DeviceComposer* composer, gralloc_handle_t* buffers,
                                        int count) {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    auto ret =
            composer->prepareDeviceFrameBuffer(mActiveConfig, mUiScaleType, buffers, count, false);
    if (ret)
        ALOGE("%s: failed to allocate composition buffer for display:%" PRIu32, __FUNCTION__, mId);

    return ret;
}

} // namespace aidl::android::hardware::graphics::composer3::impl
