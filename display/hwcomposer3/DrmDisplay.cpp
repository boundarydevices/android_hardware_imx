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

#include "DrmDisplay.h"

#include <drm_fourcc.h>
#include <stdlib.h>
#include <thread>
#include <xf86drm.h>

#include "BufferInfo.h"
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
        std::unordered_map<uint32_t, std::unique_ptr<DrmPlane>> planes,
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

    uint32_t port = 0;
    if (getDisplayPortFromProperty(connector->getName(), &port)) {
        id = port;
    }

    char planeStr[100] = {0}, tempStr[100];
    for (const auto& [planeId, _] : planes) {
        sprintf(tempStr, "%d ", planeId);
        strcat(planeStr, tempStr);
    }

    ALOGI("%s: display %d created: crtc=%d, connector=%d(%s), plane=%s", __FUNCTION__, id,
          crtc->getId(), connector->getId(), connector->getName().c_str(), planeStr);

    std::unique_ptr<DrmDisplay> display(
            new DrmDisplay(id, std::move(connector), std::move(crtc), std::move(planes)));

#ifdef DEBUG_DUMP_REFRESH_RATE
    memset(&(display->mDumpActualFps), 0, sizeof(DumpRefreshRate));
    display->mDumpActualFps.displayId = id;
#endif

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

    HalDisplayConfig config = (*mConfigs)[static_cast<uint32_t>(mActiveConfigId)];
    common::Rect& rectF = buffer->mDisplayFrame;
    common::Rect& rectS = buffer->mSourceCrop;
    uint32_t x0 = static_cast<uint32_t>(rectF.left) * config.modeWidth / config.width;
    uint32_t y0 = static_cast<uint32_t>(rectF.top) * config.modeHeight / config.height;
    uint32_t wF = static_cast<uint32_t>(rectF.right - rectF.left) * config.modeWidth / config.width;
    uint32_t hF =
            static_cast<uint32_t>(rectF.bottom - rectF.top) * config.modeHeight / config.height;
    uint32_t wS = static_cast<uint32_t>(rectS.right - rectS.left);
    uint32_t hS = static_cast<uint32_t>(rectS.bottom - rectS.top);
    // alignment is needed for imx8mq
    wF = ALIGN_PIXEL_2(wF - 1);
    hF = ALIGN_PIXEL_2(hF - 1);
    wS = ALIGN_PIXEL_2(wS - 1);
    hS = ALIGN_PIXEL_2(hS - 1);

    DrmPlane* plane = mPlanes[planeId].get();
    bool okay = true;
    okay &= request->Set(planeId, plane->getCrtcProperty(), mCrtc->getId());
    okay &= request->Set(planeId, plane->getFbProperty(), *buffer->mDrmFramebuffer);
    okay &= request->Set(planeId, plane->getCrtcXProperty(), static_cast<uint64_t>(x0));
    okay &= request->Set(planeId, plane->getCrtcYProperty(), static_cast<uint64_t>(y0));
    okay &= request->Set(planeId, plane->getCrtcWProperty(), static_cast<uint64_t>(wF));
    okay &= request->Set(planeId, plane->getCrtcHProperty(), static_cast<uint64_t>(hF));
    okay &= request->Set(planeId, plane->getSrcXProperty(),
                         static_cast<uint64_t>(rectS.left << 16));
    okay &= request->Set(planeId, plane->getSrcYProperty(), static_cast<uint64_t>(rectS.top << 16));
    okay &= request->Set(planeId, plane->getSrcWProperty(), static_cast<uint64_t>(wS << 16));
    okay &= request->Set(planeId, plane->getSrcHProperty(), static_cast<uint64_t>(hS << 16));

    auto& prop = plane->getZposProperty();
    if ((prop.getId() != (uint32_t)-1) && !(prop.getFlags() & DRM_MODE_PROP_IMMUTABLE))
        okay &= request->Set(planeId, prop, static_cast<uint64_t>(buffer->mZpos));

    DEBUG_LOG("%s: crtc:x0=%d, y0=%d, wd=%d, hd=%d, src:x0=%d, y0=%d, ws=%d, hs=%d", __func__, x0,
              y0, wF, hF, rectS.left, rectS.top, wS, hS);
    if (!okay) {
        ALOGE("%s: failed to flush to Overlay plane:%d.", __FUNCTION__, planeId);
        return std::make_tuple(HWC3::Error::NoResources, std::move(request));
    }

    mTempBuffers.planeDrmBuffer[planeId] = buffer;

    plane->setState(PLANE_STATE_ACTIVE);
    if (buffer->mZpos > mOverlayMaxZpos)
        mOverlayMaxZpos = buffer->mZpos;

    DEBUG_LOG("%s: flush overlay plane:%d, fbId=%d", __FUNCTION__, planeId,
              *buffer->mDrmFramebuffer);
    return std::make_tuple(HWC3::Error::None, std::move(request));
}

void DrmDisplay::clearTempBuffer(uint32_t overlaynum) {
    if (overlaynum < mTempBuffers.planeDrmBuffer.size()) {
        mTempBuffers.planeDrmBuffer.clear();
    }
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

    HalDisplayConfig config = (*mConfigs)[static_cast<uint32_t>(mActiveConfigId)];
    common::Rect& rectF = buffer->mDisplayFrame;
    common::Rect& rectS = buffer->mSourceCrop;
    uint32_t frameX = static_cast<uint32_t>(rectF.left) * config.modeWidth / config.width;
    uint32_t frameY = static_cast<uint32_t>(rectF.top) * config.modeHeight / config.height;
    uint32_t frameWidth =
            static_cast<uint32_t>(rectF.right - rectF.left) * config.modeWidth / config.width;
    uint32_t frameHeight =
            static_cast<uint32_t>(rectF.bottom - rectF.top) * config.modeHeight / config.height;
    uint32_t sourceX = static_cast<uint32_t>(rectS.left);
    uint32_t sourceY = static_cast<uint32_t>(rectS.top);
    uint32_t sourceWidth = static_cast<uint32_t>(rectS.right - rectS.left);
    uint32_t sourceHeight = static_cast<uint32_t>(rectS.bottom - rectS.top);

    /*
     * Display controller plane(hardware) support: DCSS(imx8mq), DPU(imx8q)
     * bootargs set like: setenv append_bootargs androidboot.gui_resolution=shw1280x720
     * Composer(g2d) in display HAL support: DCNANO(imx8ulp), LCDIF(imx8mm, imx8mp)
     * bootargs set like: setenv append_bootargs androidboot.gui_resolution=ssw1280x720
     */
    uint32_t sh, sw, dh, dw;
    if (mUiScaleType == UI_SCALE_SOFTWARE) { // UI is only a part of framebuffer
        sourceX = 0;
        sourceY = 0;
        sw = config.modeWidth;  // set actual framebuffer width
        sh = config.modeHeight; // set actual framebuffer height
        frameX = 0;
        frameY = 0;
        dw = config.modeWidth;
        dh = config.modeHeight;
    } else if (mUiScaleType == UI_SCALE_HARDWARE) {
        sw = sourceWidth;
        sh = sourceHeight;
        frameX = 0;
        frameY = 0;
        dw = config.width;  // avoid scale up
        dh = config.height; // avoid scale up
    } else {
        sw = sourceWidth;
        sh = sourceHeight;
        dw = frameWidth;
        dh = frameHeight;
    }

    DrmPlane* plane = mPlanes[planeId].get();
    bool okay = true;
    okay &= request->Set(planeId, plane->getCrtcProperty(), mCrtc->getId());
    okay &= request->Set(planeId, plane->getInFenceProperty(),
                         static_cast<uint64_t>(inSyncFd.get()));
    okay &= request->Set(planeId, plane->getFbProperty(), *buffer->mDrmFramebuffer);
    okay &= request->Set(planeId, plane->getCrtcXProperty(), static_cast<uint64_t>(frameX));
    okay &= request->Set(planeId, plane->getCrtcYProperty(), static_cast<uint64_t>(frameY));
    okay &= request->Set(planeId, plane->getCrtcWProperty(), static_cast<uint64_t>(dw));
    okay &= request->Set(planeId, plane->getCrtcHProperty(), static_cast<uint64_t>(dh));
    okay &= request->Set(planeId, plane->getSrcXProperty(), static_cast<uint64_t>(sourceX << 16));
    okay &= request->Set(planeId, plane->getSrcYProperty(), static_cast<uint64_t>(sourceY << 16));
    okay &= request->Set(planeId, plane->getSrcWProperty(), static_cast<uint64_t>(sw << 16));
    okay &= request->Set(planeId, plane->getSrcHProperty(), static_cast<uint64_t>(sh << 16));

    auto& prop = plane->getZposProperty();
    if ((prop.getId() != (uint32_t)-1) && !(prop.getFlags() & DRM_MODE_PROP_IMMUTABLE))
        okay &= request->Set(planeId, prop, static_cast<uint64_t>(mOverlayMaxZpos + 1));

    if (!okay) {
        ALOGE("%s: failed to flush Primary plane:%d.", __FUNCTION__, planeId);
        return std::make_tuple(HWC3::Error::NoResources, std::move(request));
    }

    mTempBuffers.clientTargetDrmBuffer = buffer;

    plane->setState(PLANE_STATE_ACTIVE);
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
        if (plane->getState() == PLANE_STATE_ACTIVE) {
            sprintf(tempStr, "%d ", pair.first);
            strcat(activeStr, tempStr);
            continue;
        } else if (plane->getState() == PLANE_STATE_DISABLED) {
            okay &= request->Set(plane->getId(), plane->getCrtcProperty(), 0);
            okay &= request->Set(plane->getId(), plane->getFbProperty(), 0);
            plane->setState(PLANE_STATE_NONE);
        }
        sprintf(tempStr, "%d ", pair.first);
        strcat(disableStr, tempStr);
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
        if (mConnector->getHdrMetadataProperty().isValid())
            okay &= request->Set(mConnector->getId(), mConnector->getHdrMetadataProperty(),
                                 mHdrMetadataBlobId);
        okay &= request->Set(mCrtc->getId(), mCrtc->getActiveProperty(), 1);
        okay &= request->Set(mCrtc->getId(), mCrtc->getModeProperty(), modeBlobId);
        request->setAllowModesetFlag(true);
        ALOGI("%s: *** Do modeset for display:%d ***", __FUNCTION__, mId);
    }
    okay &= request->Set(mCrtc->getId(), mCrtc->getOutFenceProperty(),
                         addressAsUint(&flushFenceFd));

    if (!okay) {
        ALOGE("%s: failed to set atomic request.", __FUNCTION__);
        return std::make_tuple(HWC3::Error::NoResources, ::android::base::unique_fd());
    }

    uint32_t vsyncPeriod = 1000000000UL / mActiveConfig.refreshRateHz;   // convert to nanosecond
#ifdef FIX_HANG_WHEN_FIRST_PLUG_IN
    if (mPreheatFrameCnt > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(vsyncPeriod / 1000000));
        mPreheatFrameCnt--;
    }
#endif
    uint32_t interval = vsyncPeriod * 2 / MAX_COMMIT_RETRY_COUNT / 1000; // try 2 Vsync period
#ifdef DEBUG_DUMP_REFRESH_RATE
    nsecs_t now = dumpRefreshRateStart();
#endif
    int ret;
    uint32_t i = 0;
    ret = request->Commit(drmFd);
    while ((ret == -EBUSY) && (i < mCommitRetryCnt)) {
        usleep(interval);
        ret = request->Commit(drmFd);
        i++;
    }
    if (ret != 0) {
        ALOGE("%s: atomic commit for display:%d failed ret=%d after retry %d times", __FUNCTION__,
              mId, ret, i);
        return std::make_tuple(HWC3::Error::NoResources, ::android::base::unique_fd());
    }
#ifdef DEBUG_DUMP_REFRESH_RATE
    dumpRefreshRateEnd(mDumpActualFps, vsyncPeriod, now);
#endif

    if (mModeSet && ret == 0) {
        mModeSet = false;
        // The first frame after modeset may cost more time to commit sucessfully
        mCommitRetryCnt = MAX_COMMIT_RETRY_COUNT * 4;
    } else {
        mCommitRetryCnt = MAX_COMMIT_RETRY_COUNT;
    }

    for (auto& [_, plane] : mPlanes) {
        if (plane->getState() == PLANE_STATE_ACTIVE) {
            plane->setState(PLANE_STATE_DISABLED);
        }
    }
    mOverlayMaxZpos = 0;

    mPreviousBuffers.clientTargetDrmBuffer = mTempBuffers.clientTargetDrmBuffer;
    mPreviousBuffers.planeDrmBuffer = mTempBuffers.planeDrmBuffer;

    DEBUG_LOG("%s: atomic commit display:%d, plane:active=%s,disabled=%s; present fence:%d, retry"
              " %d times",
              __FUNCTION__, mId, activeStr, disableStr, flushFenceFd, i);
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

    if (mCrtc->getDisplayXferProperty().isValid()) {
        mCrtc->setLowPowerDisplay(drmFd, power);
    } else {
        mConnector->setPowerMode(drmFd, power);
    }

    return true;
}

void DrmDisplay::buildPlaneIdPool(uint32_t* outTopOverlayId) {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    uint32_t maxZpos = 0;
    uint32_t maxZposOverlayId = std::numeric_limits<uint32_t>::max();
    mPlaneIdPool.clear();
    for (const auto& [planeId, plane] : mPlanes) {
        DEBUG_LOG("check plane %d type: %s", planeId, plane->isOverlay() ? "overlay" : "primary");
        if (plane->isOverlay()) {
            mPlaneIdPool.push_back(planeId);
            auto& prop = plane->getZposProperty();
            if (prop.isValid() && prop.getValue() >= maxZpos) {
                maxZpos = static_cast<uint32_t>(prop.getValue());
                maxZposOverlayId = planeId;
            }
        }
    }
    *outTopOverlayId = maxZposOverlayId;
    mOverlayPlaneNum = mPlaneIdPool.size();

    DEBUG_LOG("%s: display:%" PRIu32 " there are %zu overlay plane", __FUNCTION__, mId,
              mPlaneIdPool.size());
}

void DrmDisplay::reservePlaneId(uint32_t planeId) {
    auto it = std::find(mPlaneIdPool.begin(), mPlaneIdPool.end(), planeId);
    if (it != mPlaneIdPool.end())
        mPlaneIdPool.erase(it);
}

uint32_t DrmDisplay::findDrmPlane(const native_handle_t* handle) {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    if (mPlaneIdPool.size() == 0) {
        DEBUG_LOG("%s: overlay plane pool is empty", __FUNCTION__);
        return 0;
    }
#ifdef OVERLAY_LIMITATION_DPU
    if (mOverlayPlaneNum - mPlaneIdPool.size() >= 1) {
        DEBUG_LOG("%s: already 1 overlay plane used. Not use other overlay plane to avoid display "
                  "underrun issue", __FUNCTION__);
        return 0;
    }
#endif

    HandleInfo info;
    if (!handle || (getInfoFromHandle(handle, &info) != 0)) {
        ALOGE("%s: display:%" PRIu32 " invalid native_handle", __FUNCTION__, mId);
        return 0;
    }

    uint32_t format = info.drm_format;
    uint64_t modifier = info.modifier;
#ifdef DEBUG_NXP_HWC
    {
        char fmt[6];
        char* name = drmGetFormatName(format, fmt); // defined in HWC, no malloc memory
        char* modifier_name = drmGetFormatModifierName(modifier);
        DEBUG_LOG("%s: Checking buffer:%s :%s %s", __FUNCTION__, info.name, name, modifier_name);
        free(modifier_name);
    }
#endif
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
    uint32_t delta = UINT_MAX, rdelta = UINT_MAX;
    uint32_t dst_width = 0, dst_height = 0, dst_vrefresh = 60, prefered_mode = 0;
    // get display mode from bootargs.
    parseDisplayMode(&dst_width, &dst_height, &dst_vrefresh, &prefered_mode);

    for (uint32_t i = 0; i < configs->size(); i++) {
        auto& cfg = (*configs)[static_cast<uint32_t>(mStartConfigId) + i];
        if ((prefered_mode == 1) && (cfg.modeType & DRM_MODE_TYPE_PREFERRED)) {
            index = i;
            break;
        }

        rdelta = static_cast<uint32_t>(abs((int)dst_width - (int)cfg.width) +
                                       abs((int)dst_height - (int)cfg.height));
        if (rdelta < delta) {
            delta = rdelta;
            index = i;
        } else if (rdelta == delta) {
            auto& prev = (*configs)[static_cast<uint32_t>(mStartConfigId) + index];
            if (abs((int)dst_vrefresh - (int)cfg.refreshRateHz) <
                abs((int)dst_vrefresh - (int)prev.refreshRateHz))
                index = i;
        }
    }

    mActiveConfigId = mStartConfigId + static_cast<int32_t>(index);
    mInitActiveConfigId = mActiveConfigId;
    auto activeConfig = (*configs)[static_cast<uint32_t>(mActiveConfigId)];
    ALOGI("Find best mode w:%d, h:%d, refreshrate:%d at mode index %d", activeConfig.width,
          activeConfig.height, activeConfig.refreshRateHz, index);

    // Because Chrome will switch different display config according to content refresh rate
    // requirement, it cause screen blank and unblank again when change display config.
    // TODO: our platform cannot change display config seamlessly, so remove the display config
    //       with other refresh rate.
    auto check_fun = [&](const auto& pair) -> bool {
        if (pair.second.refreshRateHz != activeConfig.refreshRateHz)
            return true;
        return false;
    };
    auto it = std::find_if(configs->begin(), configs->end(), check_fun);
    while (it != configs->end()) {
        configs->erase(it->first);
        it = std::find_if(configs->begin(), configs->end(), check_fun);
    }

    uint32_t width = activeConfig.width;
    uint32_t height = activeConfig.height;
    if (customizeGUIResolution(width, height, &mUiScaleType)) {
        HalDisplayConfig newConfig;
        memset(&newConfig, 0, sizeof(newConfig));
        newConfig.width = width;
        newConfig.height = height;
        newConfig.dpiX = 160;
        newConfig.dpiY = 160;
        newConfig.refreshRateHz = activeConfig.refreshRateHz;
        newConfig.blobId = activeConfig.blobId;
        newConfig.modeWidth = activeConfig.width;
        newConfig.modeHeight = activeConfig.height;

        uint32_t id_max = 0;
        for (auto& [id, cfg] : *configs) {
            if (id > id_max)
                id_max = id;
        }

        mActiveConfigId = mStartConfigId + static_cast<int32_t>(id_max) + 1;
        mInitActiveConfigId = mActiveConfigId;
        configs->emplace(mActiveConfigId, newConfig);
        DEBUG_LOG("%s: Add new config:%d x %d, fps=%d, mode=%d x %d", __FUNCTION__, newConfig.width,
                  newConfig.height, newConfig.refreshRateHz, newConfig.modeWidth,
                  newConfig.modeHeight);
    }
    mActiveConfig = (*configs)[static_cast<uint32_t>(mActiveConfigId)];

    uint32_t format;
    getFramebufferInfo(&width, &height, &format);
    ALOGI("Display Id   = %d \n"
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

    uint32_t id_max = 0;
    for (auto& [id, cfg] : *mConfigs) {
        if (id > id_max)
            id_max = id;
    }
    mStartConfigId = mStartConfigId + static_cast<int32_t>(id_max) + 1;
    mConfigs->clear();
    if (mConnector->buildConfigs(mConfigs, static_cast<uint32_t>(mStartConfigId))) {
        updateActiveConfig(mConfigs);
    }

    return true;
}

void DrmDisplay::placeholderDisplayConfigs() {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    mStartConfigId = mStartConfigId + static_cast<int32_t>(mConfigs->size());
    mConfigs->clear();

    HalDisplayConfig newConfig;
    memset(&newConfig, 0, sizeof(newConfig));
    if (mActiveConfigId >= 0) {
        memcpy(&newConfig, &mActiveConfig, sizeof(HalDisplayConfig));
        newConfig.blobId = 0;
        newConfig.modeWidth = 0;
        newConfig.modeHeight = 0;
    } else {
#ifdef MAX_DRM_CONFIG_4K
        newConfig.width = 3840;
        newConfig.height = 2160;
#elif MAX_DRM_CONFIG_720P
        newConfig.width = 1280; // display driver of 8ulp only support max 720x1280.
        newConfig.height = 720; // such limitation will affect DRM checking when create DRM buffer
#else
        newConfig.width = 1920;
        newConfig.height = 1080;
#endif
        newConfig.dpiX = 160;
        newConfig.dpiY = 160;
        newConfig.refreshRateHz = 60;
#ifdef FIX_HANG_WHEN_FIRST_PLUG_IN
        mPreheatFrameCnt = 2;
        if (mConnector->getEncoderType() == DRM_MODE_ENCODER_LVDS) {
            newConfig.width = 1280;
            newConfig.height = 720;
        }
#endif
    }

    mConfigs->emplace(mStartConfigId, newConfig);
    mActiveConfigId = mStartConfigId;
    mInitActiveConfigId = mActiveConfigId;

    mActiveConfig = (*mConfigs)[static_cast<uint32_t>(mActiveConfigId)];
}

bool DrmDisplay::setActiveConfigId(int32_t configId) {
    DEBUG_LOG("%s: display:%" PRIu32 " configId=%d", __FUNCTION__, mId, configId);

    if (mConfigs->find(static_cast<uint32_t>(configId)) == mConfigs->end()) {
        ALOGE("%s: the config Id=%d is invalid", __FUNCTION__, configId);
        return false;
    }

    mActiveConfigId = configId;
    mActiveConfig = (*mConfigs)[static_cast<uint32_t>(mActiveConfigId)];
    mModeSet = true; // make this config effect when commit next framebuffer

    return true;
}

bool DrmDisplay::resetDisplayConfig() {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    return setActiveConfigId(mInitActiveConfigId);
}

void DrmDisplay::updateFramebufferFormat() {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    uint32_t id = getPrimaryPlaneId();
    DrmPlane* plane = mPlanes[id].get();
    std::string cfg_format = getFramebufferFormat();
    if (cfg_format != "") {
        uint32_t fmt = 0, drm_fmt = 0;
        uint64_t modifier = 0;
        if (cfg_format == "RGBA_8888") {
            fmt = static_cast<uint32_t>(common::PixelFormat::RGBA_8888);
        } else if (cfg_format == "RGBX_8888") {
            fmt = static_cast<uint32_t>(common::PixelFormat::RGBX_8888);
        } else if (cfg_format == "RGB_888") {
            fmt = static_cast<uint32_t>(common::PixelFormat::RGB_888);
        } else if (cfg_format == "RGB_565") {
            fmt = static_cast<uint32_t>(common::PixelFormat::RGB_565);
        } else if (cfg_format == "BGRA_8888") {
            fmt = static_cast<uint32_t>(common::PixelFormat::BGRA_8888);
        }
        if (fmt != 0) {
            drm_fmt = ConvertNxpFormatToDrmFormat(fmt, &modifier);
            if (plane->checkFormatSupported(drm_fmt)) {
                ALOGI("%s: display:%d configure framebuffer format as %s", __FUNCTION__, mId,
                      cfg_format.c_str());
                mFbFormat = fmt;
                return;
            }
        }
    }

    if (plane->checkFormatSupported(DRM_FORMAT_ABGR8888)) {
        mFbFormat = static_cast<uint32_t>(common::PixelFormat::RGBA_8888);
    } else if (plane->checkFormatSupported(DRM_FORMAT_XBGR8888)) {
        mFbFormat = static_cast<uint32_t>(common::PixelFormat::RGBX_8888);
    } else if (plane->checkFormatSupported(DRM_FORMAT_ARGB8888)) {
        // primary plane of imx8ulp use such format
        mFbFormat = static_cast<uint32_t>(common::PixelFormat::BGRA_8888);
    } else if (plane->checkFormatSupported(DRM_FORMAT_RGB565)) {
        mFbFormat = static_cast<uint32_t>(common::PixelFormat::RGB_565);
    }
}
int DrmDisplay::getFramebufferInfo(uint32_t* width, uint32_t* height, uint32_t* format) {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    if (mUiScaleType == UI_SCALE_SOFTWARE) {
        *width = mActiveConfig.modeWidth;
        *height = mActiveConfig.modeHeight;
    } else {
        *width = mActiveConfig.width;
        *height = mActiveConfig.height;
    }

    *format = mFbFormat;
    return 0;
}

bool DrmDisplay::setSecureMode(::android::base::borrowed_fd drmFd, bool secure) {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    int val = secure ? 1 : 0;
    mConnector->setHDCPMode(drmFd, val);

    return true;
}

bool DrmDisplay::setHdrMetadataBlobId(uint32_t bolbId) {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    mHdrMetadataBlobId = bolbId;
    mModeSet = true;

    return true;
}

} // namespace aidl::android::hardware::graphics::composer3::impl
