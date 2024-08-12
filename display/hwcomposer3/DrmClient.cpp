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

#include "DrmClient.h"

#include <cutils/properties.h>
#include <drm_fourcc.h>
#include <hwsecure_client.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "BufferInfo.h"
#include "Common.h"
#include "Drm.h"

namespace aidl::android::hardware::graphics::composer3::impl {

DrmClient::~DrmClient() {
    if (mFd.ok() && drmIsMaster(mFd.get())) {
        drmDropMaster(mFd.get());
    }
}

HWC3::Error DrmClient::init(char* path, uint32_t* baseId) {
    DEBUG_LOG("%s", __FUNCTION__);

    mFd = ::android::base::unique_fd(open(path, O_RDWR | O_CLOEXEC));
    if (!mFd.ok()) {
        ALOGE("%s: failed to open drm device: %s", __FUNCTION__, strerror(errno));
        return HWC3::Error::NoResources;
    }

    int ret = drmSetClientCap(mFd.get(), DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    if (ret) {
        ALOGE("%s: failed to set cap universal plane %s\n", __FUNCTION__, strerror(errno));
        return HWC3::Error::NoResources;
    }

    ret = drmSetClientCap(mFd.get(), DRM_CLIENT_CAP_ATOMIC, 1);
    if (ret) {
        ALOGE("%s: failed to set cap atomic %s\n", __FUNCTION__, strerror(errno));
        return HWC3::Error::NoResources;
    }

    drmSetMaster(mFd.get());

    if (!drmIsMaster(mFd.get())) {
        ALOGE("%s: failed to get master drm device", __FUNCTION__);
        return HWC3::Error::NoResources;
    }

    uint32_t displayBaseId = 0;
    drmVersionPtr version = drmGetVersion(mFd);
    if (version) {
        if (!strncmp(version->name, "mxsfb-drm", strlen("mxsfb-drm")))
            // display port, used to identify framebuffer usage in framework(FramebufferSurface.cpp)
            displayBaseId = 0x40;
        else
            displayBaseId = 0;

        drmFreeVersion(version);
    }

    {
        std::lock_guard<std::recursive_mutex> lock(mDisplaysMutex);
        bool success = loadDrmDisplays(displayBaseId);
        if (success) {
            DEBUG_LOG("%s: Successfully initialized DRM backend", __FUNCTION__);
        } else {
            ALOGE("%s: Failed to initialize DRM backend", __FUNCTION__);
            return HWC3::Error::NoResources;
        }
    }

    uint32_t overlayTotalNum = 0;
    for (auto& [_, display] : mDisplays) {
        overlayTotalNum += display->getPlaneNum() - 1; // At least one primary plane for each
    }
    std::size_t framebufferCacheSize = mMaxComposerTargetsPerDisplay * mDisplays.size();
    mPlaneBufferCacheSize = IsOverlayUserDisabled() ? 0 : (overlayTotalNum * 18);

    DEBUG_LOG("%s: initializing DRM Buffer cache size for framebuffer=%zu, for plane buffer=%zu",
              __FUNCTION__, framebufferCacheSize, mPlaneBufferCacheSize);
    mFramebufferCache = std::make_unique<DrmBufferCache>(framebufferCacheSize);
    if (mPlaneBufferCacheSize > 0)
        mPlaneBufferCache = std::make_unique<DrmBufferCache>(mPlaneBufferCacheSize);

    mDrmEventListener = DrmEventListener::create(mFd, [this]() { handleHotplug(); });
    if (!mDrmEventListener) {
        ALOGE("%s: Failed to initialize DRM event listener", __FUNCTION__);
    } else {
        DEBUG_LOG("%s: Successfully initialized DRM event listener", __FUNCTION__);
    }

    *baseId = displayBaseId;
    mDisplayBaseId = displayBaseId;

    int cnt = loadBacklightDevices();
    ALOGI("%s: There are %d backlight devices", __FUNCTION__, cnt);

    DEBUG_LOG("%s: Successfully initialized.", __FUNCTION__);
    return HWC3::Error::None;
}

HWC3::Error DrmClient::getDisplayConfigs(std::vector<HalMultiConfigs>* configs) {
    DEBUG_LOG("%s", __FUNCTION__);

    std::lock_guard<std::recursive_mutex> lock(mDisplaysMutex);

    configs->clear();

    for (const auto& pair : mDisplays) {
        DrmDisplay* display = pair.second.get();
        if (!display->isConnected() && !display->isPrimary()) {
            continue;
        }

        configs->emplace_back(HalMultiConfigs{
                .displayId = display->getId(),
                .activeConfigId = display->getActiveConfigId(),
                .configs = display->getDisplayConfigs(),
        });
    }

    ALOGI("%s: %zu displays in DRM Client:%d, get %zu configs", __FUNCTION__, mDisplays.size(),
          mFd.get(), configs->size());
    if (configs->size() > 0)
        return HWC3::Error::None;
    else
        return HWC3::Error::NoResources;
}

HWC3::Error DrmClient::registerOnHotplugCallback(const HotplugCallback& cb) {
    mHotplugCallback = cb;
    return HWC3::Error::None;
}

HWC3::Error DrmClient::unregisterOnHotplugCallback() {
    mHotplugCallback.reset();
    return HWC3::Error::None;
}

bool DrmClient::loadDrmDisplays(uint32_t displayBaseId) {
    DEBUG_LOG("%s", __FUNCTION__);

    std::vector<std::unique_ptr<DrmCrtc>> crtcs;
    std::vector<std::unique_ptr<DrmConnector>> connectors;
    std::vector<std::unique_ptr<DrmPlane>> planes;

    drmModePlaneResPtr drmPlaneResources = drmModeGetPlaneResources(mFd.get());
    for (uint32_t i = 0; i < drmPlaneResources->count_planes; ++i) {
        const uint32_t planeId = drmPlaneResources->planes[i];

        auto plane = DrmPlane::create(mFd, planeId);
        if (!plane) {
            ALOGE("%s: Failed to create DRM CRTC.", __FUNCTION__);
            drmModeFreePlaneResources(drmPlaneResources);
            return false;
        }

        planes.emplace_back(std::move(plane));
    }
    drmModeFreePlaneResources(drmPlaneResources);

    drmModeRes* drmResources = drmModeGetResources(mFd.get());
    for (int crtcIndex = 0; crtcIndex < drmResources->count_crtcs; crtcIndex++) {
        const uint32_t crtcId = drmResources->crtcs[crtcIndex];

        auto crtc = DrmCrtc::create(mFd, crtcId, crtcIndex);
        if (!crtc) {
            ALOGE("%s: Failed to create DRM CRTC.", __FUNCTION__);
            return false;
        }

        crtcs.emplace_back(std::move(crtc));
    }

    for (int i = 0; i < drmResources->count_connectors; ++i) {
        const uint32_t connectorId = drmResources->connectors[i];

        auto connector = DrmConnector::create(mFd, connectorId);
        if (!connector) {
            ALOGE("%s: Failed to create DRM CRTC.", __FUNCTION__);
            return false;
        }

        connectors.emplace_back(std::move(connector));
    }

    drmModeFreeResources(drmResources);

    ALOGI("%s: there are %zu crtcs, %zu connectors, %zu planes in DrmClient:%d", __FUNCTION__,
          crtcs.size(), connectors.size(), planes.size(), mFd.get());
    if (crtcs.size() < connectors.size()) {
        ALOGE("%s: Failed assumption mCrtcs.size():%zu larger than or equal mConnectors.size():%zu",
              __FUNCTION__, crtcs.size(), connectors.size());
        return false;
    }

    uint32_t numPlaneInCrtc = (planes.size() + connectors.size() - 1) / connectors.size();
    std::unordered_map<uint32_t, std::unique_ptr<DrmPlane>> crtc_planes;
    for (uint32_t i = 0; i < connectors.size(); i++) {
        std::unique_ptr<DrmConnector> connector = std::move(connectors[i]);

        auto crtcIt =
                std::find_if(crtcs.begin(), crtcs.end(), [&](const std::unique_ptr<DrmCrtc>& crtc) {
                    return connector->isCompatibleWith(*crtc);
                });
        if (crtcIt == crtcs.end()) {
            ALOGE("%s: Failed to find crtc for connector:%d", __FUNCTION__, connector->getId());
            return false;
        }
        std::unique_ptr<DrmCrtc> crtc = std::move(*crtcIt);
        crtcs.erase(crtcIt);

        uint32_t cnt = 0;
        auto check_fun = [&](std::unique_ptr<DrmPlane>& plane) -> bool {
            if (!plane->isOverlay() && !plane->isPrimary()) {
                return false;
            }
            if (plane->isCompatibleWith(*crtc) && cnt < numPlaneInCrtc) {
                crtc_planes.insert({plane->getId(), std::move(plane)});
                cnt++;
                return true;
            } else {
                return false;
            }
        };
        auto it = std::find_if(planes.begin(), planes.end(), check_fun);
        while (it != planes.end()) {
            planes.erase(it);
            it = std::find_if(planes.begin(), planes.end(), check_fun);
        }
        if (crtc_planes.size() == 0) {
            ALOGE("%s: Failed to find plane for display:%" PRIu32, __FUNCTION__, i);
            return false;
        }

        auto display = DrmDisplay::create(displayBaseId + i, std::move(connector), std::move(crtc),
                                          crtc_planes, mFd);
        if (!display) {
            return false;
        }
        display->updateDisplayConfigs();
        mDisplays.emplace(display->getId(), std::move(display));
    }

    if (mDisplays.size() > 0)
        return true;
    else
        return false;
}

std::tuple<HWC3::Error, std::shared_ptr<DrmBuffer>> DrmClient::create(const native_handle_t* handle,
                                                                      common::Rect displayFrame,
                                                                      common::Rect sourceCrop,
                                                                      BufferType type) {
    ATRACE_CALL();

    HandleInfo info;
    if (handle == nullptr || (getInfoFromHandle(handle, &info) != 0)) {
        ALOGE("%s: invalid native handle", __FUNCTION__);
        return std::make_tuple(HWC3::Error::BadParameter, nullptr);
    }

    DrmPrimeBufferHandle primeHandle = 0;
    int ret = drmPrimeFDToHandle(mFd.get(), info.fd, &primeHandle);
    if (ret) {
        ALOGE("%s: drmPrimeFDToHandle failed: %s (errno %d)", __FUNCTION__, strerror(errno), errno);
        return std::make_tuple(HWC3::Error::NoResources, nullptr);
    }

    std::shared_ptr<DrmBuffer>* drmBufferPtr = nullptr;
    if (type == DRM_BUFFER_FB)
        drmBufferPtr = mFramebufferCache->get(primeHandle);
    else if ((type == DRM_BUFFER_PLANE) && (mPlaneBufferCacheSize > 0)) {
        drmBufferPtr = mPlaneBufferCache->get(primeHandle);
    }

    if (drmBufferPtr != nullptr) {
        (*drmBufferPtr)->mDisplayFrame = displayFrame;
        (*drmBufferPtr)->mSourceCrop = sourceCrop;
        DEBUG_LOG("%s: found framebuffer:%" PRIu32, __FUNCTION__,
                  *(*drmBufferPtr)->mDrmFramebuffer);
        return std::make_tuple(HWC3::Error::None, std::shared_ptr<DrmBuffer>(*drmBufferPtr));
    }

    auto buffer = std::shared_ptr<DrmBuffer>(new DrmBuffer(*this));
    buffer->mWidth = info.width;
    buffer->mHeight = info.height;
    buffer->mDisplayFrame = displayFrame;
    buffer->mSourceCrop = sourceCrop;
    buffer->mDrmFormat = info.drm_format;
    buffer->mPlaneFds[0] = info.fd;
    for (uint32_t i = 0; i < info.num_planes; i++) {
        buffer->mPlaneHandles[i] = primeHandle;
        buffer->mPlanePitches[i] = info.strides[i];
        buffer->mPlaneOffsets[i] = info.offsets[i];
        buffer->mPlaneModifiers[i] = info.modifier;
    }

    uint32_t framebuffer = 0;
    if (buffer->mPlaneModifiers[0] > 0) {
        ret = drmModeAddFB2WithModifiers(mFd.get(), buffer->mWidth, buffer->mHeight,
                                         buffer->mDrmFormat, buffer->mPlaneHandles,
                                         buffer->mPlanePitches, buffer->mPlaneOffsets,
                                         buffer->mPlaneModifiers, &framebuffer,
                                         DRM_MODE_FB_MODIFIERS);
    } else {
        ret = drmModeAddFB2(mFd.get(), buffer->mWidth, buffer->mHeight, buffer->mDrmFormat,
                            buffer->mPlaneHandles, buffer->mPlanePitches, buffer->mPlaneOffsets,
                            &framebuffer, 0);
    }
    if (ret) {
        ALOGE("%s: drmModeAddFB2 failed(buffer:size=%d, %d x %d, stride=%d, format=0x%x,"
              "drm_format=0x%x, modifier=0x%" PRIx64 "): %s (errno %d)",
              __FUNCTION__, info.size, info.width, info.height, info.stride, info.format,
              info.drm_format, buffer->mPlaneModifiers[0], strerror(errno), errno);
        return std::make_tuple(HWC3::Error::NoResources, nullptr);
    }
    DEBUG_LOG("%s: created framebuffer:%" PRIu32, __FUNCTION__, framebuffer);
    buffer->mDrmFramebuffer = framebuffer;

    if (type == DRM_BUFFER_FB)
        mFramebufferCache->set(primeHandle, std::shared_ptr<DrmBuffer>(buffer));
    else if ((type == DRM_BUFFER_PLANE) && (mPlaneBufferCacheSize > 0))
        mPlaneBufferCache->set(primeHandle, std::shared_ptr<DrmBuffer>(buffer));
    else
        ALOGW("%s: Drm Buffer(type=%d, fbId=%" PRIu32 ") is not cached", __FUNCTION__, type,
              framebuffer);

    return std::make_tuple(HWC3::Error::None, std::move(buffer));
}

HWC3::Error DrmClient::destroyDrmFramebuffer(DrmBuffer* buffer) {
    if (buffer->mDrmFramebuffer) {
        uint32_t framebuffer = *buffer->mDrmFramebuffer;
        if (drmModeRmFB(mFd.get(), framebuffer)) {
            ALOGE("%s: drmModeRmFB failed: %s (errno %d)", __FUNCTION__, strerror(errno), errno);
            return HWC3::Error::NoResources;
        }
        DEBUG_LOG("%s: destroyed framebuffer:%" PRIu32, __FUNCTION__, framebuffer);
        buffer->mDrmFramebuffer.reset();
    }
    if (buffer->mPlaneHandles[0]) {
        struct drm_gem_close gem_close = {};
        gem_close.handle = buffer->mPlaneHandles[0];
        if (drmIoctl(mFd.get(), DRM_IOCTL_GEM_CLOSE, &gem_close)) {
            ALOGE("%s: DRM_IOCTL_GEM_CLOSE failed: %s (errno %d)", __FUNCTION__, strerror(errno),
                  errno);
            return HWC3::Error::NoResources;
        }
    }

    return HWC3::Error::None;
}

bool DrmClient::handleHotplug() {
    DEBUG_LOG("%s", __FUNCTION__);

    struct HotplugToReport {
        std::unique_ptr<HalMultiConfigs> config;
        bool connected;
    };

    std::vector<HotplugToReport> hotplugs;

    TimePoint now = std::chrono::steady_clock::now();
    std::this_thread::sleep_until(now + std::chrono::milliseconds(32));

    {
        std::lock_guard<std::recursive_mutex> lock(mDisplaysMutex);

        for (auto& pair : mDisplays) {
            DrmDisplay* display = pair.second.get();
            auto change = display->checkAndHandleHotplug(mFd);
            if (change == DrmHotplugChange::kNoChange) {
                continue;
            }

            if (display->isPrimary() || (change == DrmHotplugChange::kDisconnected)) {
                uint32_t id = display->getId();
                if (mComposerTargets.find(id) != mComposerTargets.end()) {
                    // free device composer target buffers when disconnected
                    mG2dComposer->freeDeviceFrameBuffer(mComposerTargets[id].handles);
                    mComposerTargets.erase(id);
                }
            }
            if (change == DrmHotplugChange::kDisconnected) {
                if (display->isPrimary()) {
                    // primary display cannot be disconnected when report
                    // power off when disconnected (for dcss of imx8mq)
                    change = DrmHotplugChange::kConnected;
                    display->setPowerMode(mFd, DrmPower::kPowerOff);
                    display->placeholderDisplayConfigs();
                    ALOGW("primary display cannot hotplug");
                }
            }

            std::unique_ptr<HalMultiConfigs> cfg(new HalMultiConfigs{
                    .displayId = display->getId(),
                    .activeConfigId = display->getActiveConfigId(),
                    .configs = display->getDisplayConfigs(),
            });
            hotplugs.push_back(HotplugToReport{
                    .config = std::move(cfg),
                    .connected = change == DrmHotplugChange::kConnected,
            });
        }
    }

    for (auto& hotplug : hotplugs) {
        if (mHotplugCallback) {
            (*mHotplugCallback)(hotplug.connected, std::move(hotplug.config));
        }
    }

    return true;
}

std::tuple<HWC3::Error, ::android::base::unique_fd> DrmClient::flushToDisplay(
        int displayId, const DisplayBuffer& buffer, ::android::base::borrowed_fd inSyncFd) {
    ATRACE_CALL();

    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return std::make_tuple(HWC3::Error::BadDisplay, ::android::base::unique_fd());
    }
    if (mPlaneBufferCache && mPlaneBufferCache->getSize() > 0) {
        TimePoint now = std::chrono::steady_clock::now();
        if (buffer.planeDrmBuffer.size() > 0) {
            mLastPlaneBufferPresentTime = now;
        } else if (now > mLastPlaneBufferPresentTime + Nanoseconds(2000000000)) {
            mPlaneBufferCache->clear();
            ALOGI("%s: DrmBuffer cache for plane is cleared!", __FUNCTION__);
        }
    }
    if (!mDisplays[displayId]->isConnected()) {
        ALOGI("%s: %d display is disconnected, avoid DRM committing", __FUNCTION__, displayId);
        return std::make_tuple(HWC3::Error::None, ::android::base::unique_fd());
    }

    std::unique_ptr<DrmAtomicRequest> request;
    for (auto& pair : buffer.planeDrmBuffer) {
        auto [err, req] =
                mDisplays[displayId]->flushOverlay(pair.first, std::move(request), pair.second);
        if (err != HWC3::Error::None) {
            ALOGE("%s: failed, flush overlay plane:%d failed.", __FUNCTION__, pair.first);
            return std::make_tuple(HWC3::Error::NoResources, ::android::base::unique_fd());
        }
        request = std::move(req);
    }
    uint32_t primaryPlane = mDisplays[displayId]->getPrimaryPlaneId();
    if (buffer.clientTargetDrmBuffer) {
        auto [err, req] =
                mDisplays[displayId]->flushPrimary(primaryPlane, std::move(request), inSyncFd,
                                                   buffer.clientTargetDrmBuffer);
        if (err != HWC3::Error::None) {
            ALOGE("%s: failed, flush primary plane:%d failed.", __FUNCTION__, primaryPlane);
            return std::make_tuple(HWC3::Error::NoResources, ::android::base::unique_fd());
        }
        request = std::move(req);
    }

    auto [error, outFence] = mDisplays[displayId]->commit(std::move(request), mFd);
    if (mExpiredTargets.find(displayId) != mExpiredTargets.end()) {
        mG2dComposer->freeDeviceFrameBuffer(mExpiredTargets[displayId]);
        mExpiredTargets.erase(displayId);
    }

    return std::make_tuple(error, std::move(outFence));
}

std::optional<std::vector<uint8_t>> DrmClient::getEdid(uint32_t displayId) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return std::nullopt;
    }

    return mDisplays[displayId]->getEdid(mFd);
}

HWC3::Error DrmClient::setPowerMode(int displayId, DrmPower power) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    if (!mDisplays[displayId]->isConnected() && power == DrmPower::kPowerOn)
        return HWC3::Error::None;

    mDisplays[displayId]->setPowerMode(mFd, power);

    return HWC3::Error::None;
}

std::tuple<HWC3::Error, bool> DrmClient::isOverlaySupport(int displayId) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return std::make_tuple(HWC3::Error::BadDisplay, false);
    }

    bool supported = !IsOverlayUserDisabled() && (mDisplays[displayId]->getPlaneNum() > 1);
    return std::make_tuple(HWC3::Error::None, supported);
}

HWC3::Error DrmClient::checkOverlayLimitation(int displayId, Layer* layer) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    // rotation limitation
    if (layer->getTransform() != common::Transform::NONE) {
        DEBUG_LOG("%s: layer %" PRId64 " transform(%d) check failed", __FUNCTION__, layer->getId(),
                  static_cast<int>(layer->getTransform()));
        return HWC3::Error::Unsupported;
    }

    HandleInfo info;
    auto buff = layer->getBuffer().getBuffer();
    if (!buff || (getInfoFromHandle(buff, &info) != 0)) {
        return HWC3::Error::BadParameter;
    }

    // format limitation
    if ((info.format >= static_cast<uint32_t>(common::PixelFormat::RGBA_8888)) &&
        (info.format <= static_cast<uint32_t>(common::PixelFormat::BGRA_8888))) {
        DEBUG_LOG("%s: layer %" PRId64 " buffer format(0x%x) check failed", __FUNCTION__,
                  layer->getId(), info.format);
        return HWC3::Error::Unsupported;
    }

    common::Rect rect = layer->getDisplayFrame();
    auto& config = mDisplays[displayId]->getActiveConfig();
    int w = (rect.right - rect.left) * config.modeWidth / config.width;
    int h = (rect.bottom - rect.top) * config.modeHeight / config.height;
    common::Rect srect = layer->getSourceCropInt();
    int srcW = srect.right - srect.left;
    int srcH = srect.bottom - srect.top;

#ifdef OVERLAY_LIMITATION_DCSS
    // scaling limitation
    if (w > srcW * 7 || h > srcH * 7) {
        DEBUG_LOG(
                "%s: layer %ld scaling(src: %d x %d, dst: %d x %d, upscale more than 7 times) check failed",
                __FUNCTION__, layer->getId(), srcW, srcH, w, h);
        return HWC3::Error::Unsupported;
    }

    if (srcW < 64 &&
        ((info.drm_format == DRM_FORMAT_NV12) || (info.drm_format == DRM_FORMAT_NV21) ||
         (info.drm_format == DRM_FORMAT_P010))) {
        DEBUG_LOG("%s: layer %" PRId64 " small resolution(src width=%d, format=0x%x) check failed",
                  __FUNCTION__, layer->getId(), srcW, info.drm_format);
        return HWC3::Error::Unsupported;
    } else if (srcW < 32 &&
               ((info.drm_format == DRM_FORMAT_UYVY) || (info.drm_format == DRM_FORMAT_VYUY) ||
                (info.drm_format == DRM_FORMAT_YUYV) || (info.drm_format == DRM_FORMAT_YVYU))) {
        DEBUG_LOG("%s: layer %" PRId64 " small resolution(src width=%d, format=0x%x) check failed",
                  __FUNCTION__, layer->getId(), srcW, info.drm_format);
        return HWC3::Error::Unsupported;
    } else if (srcW < 16 || srcH < 8) {
        DEBUG_LOG("%s: layer %" PRId64 " small resolution(src width=%d, height=%d) check failed",
                  __FUNCTION__, layer->getId(), srcW, srcH);
        return HWC3::Error::Unsupported;
    }
#endif
#ifdef OVERLAY_LIMITATION_DPU
    if ((srcW != w) || (srcH != h)) {
        // DPU of imx95 don't support scaling(TODO: support down-scaling in later B0 chip)
        DEBUG_LOG("%s: layer %" PRId64 " scaling(src: %d x %d, dst: %d x %d) check failed",
                  __FUNCTION__, layer->getId(), srcW, srcH, w, h);
        return HWC3::Error::Unsupported;
    }

    // check buffer resolution
    if (info.width > 8192 || info.height > 8192 || info.width < 60 || info.height < 60) {
        DEBUG_LOG("%s: layer %" PRId64 " no-scaling resolution(src: %d x %d) check failed",
                  __FUNCTION__, layer->getId(), srcW, srcH);
        return HWC3::Error::Unsupported;
    }
#endif
    DEBUG_LOG("%s: Overlay check pass for layer=%" PRId64, __FUNCTION__, layer->getId());

    return HWC3::Error::None;
}

HWC3::Error DrmClient::prepareDrmPlanesForValidate(int displayId, uint32_t* uiPlaneBackup) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    uint32_t topOverlayId = uint32_t(-1);
    mDisplays[displayId]->buildPlaneIdPool(&topOverlayId);
    if ((uiPlaneBackup != nullptr) && (topOverlayId != uint32_t(-1))) {
        mDisplays[displayId]->reservePlaneId(topOverlayId);
        *uiPlaneBackup = topOverlayId;
    }

    return HWC3::Error::None;
}

std::tuple<HWC3::Error, uint32_t> DrmClient::getPlaneForLayerBuffer(int displayId,
                                                                    buffer_handle_t handle) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return std::make_tuple(HWC3::Error::BadDisplay, 0);
    }
    if (handle == NULL) {
        DEBUG_LOG("%s: empty buffer handle, not check plane", __FUNCTION__);
        return std::make_tuple(HWC3::Error::BadParameter, 0);
    }

#ifdef DEBUG_NXP_HWC
    char* name = nullptr;
    HandleInfo info;
    if (handle && (getInfoFromHandle(handle, &info) == 0))
        name = info.name;
#endif
    uint32_t planeId = mDisplays[displayId]->findDrmPlane(handle);
    if (planeId > 0) {
        DEBUG_LOG("%s: display:%" PRIu32 " Found plane=%d for buffer:%s", __FUNCTION__, displayId,
                  planeId, name);
        return std::make_tuple(HWC3::Error::None, planeId);
    } else {
        return std::make_tuple(HWC3::Error::NoResources, 0);
    }
}

HWC3::Error DrmClient::setPrimaryDisplay(int displayId) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    DrmDisplay* display = mDisplays[displayId].get();
    display->setAsPrimary(true);

    if (!display->isConnected())
        display->placeholderDisplayConfigs();

    return HWC3::Error::None;
}

HWC3::Error DrmClient::setActiveConfigId(int displayId, int32_t configId) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    uint32_t width, height, pre_width, pre_height, format;
    mDisplays[displayId]->getFramebufferInfo(&pre_width, &pre_height, &format);

    if (!mDisplays[displayId]->setActiveConfigId(configId))
        return HWC3::Error::BadParameter;

    mDisplays[displayId]->getFramebufferInfo(&width, &height, &format);
    if (((pre_width != width) || (pre_height != height)) &&
        mComposerTargets.find(displayId) != mComposerTargets.end()) {
        // need to free device composer target buffers when resolution changed
        mComposerTargets[displayId].valid = false;
    }

    return HWC3::Error::None;
}

HWC3::Error DrmClient::resetDisplayConfig(int displayId) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    uint32_t width, height, pre_width, pre_height, format;
    mDisplays[displayId]->getFramebufferInfo(&pre_width, &pre_height, &format);

    mDisplays[displayId]->resetDisplayConfig();

    mDisplays[displayId]->getFramebufferInfo(&width, &height, &format);
    if (((pre_width != width) || (pre_height != height)) &&
        mComposerTargets.find(displayId) != mComposerTargets.end()) {
        // need to free device composer target buffers when resolution changed
        mComposerTargets[displayId].valid = false;
    }

    return HWC3::Error::None;
}

std::tuple<HWC3::Error, buffer_handle_t> DrmClient::getComposerTarget(
        std::shared_ptr<DeviceComposer> composer, int displayId, bool secure) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return std::make_tuple(HWC3::Error::BadDisplay, nullptr);
    }

    std::lock_guard<std::recursive_mutex> lock(mDisplaysMutex);

    if (mComposerTargets.find(displayId) != mComposerTargets.end() &&
        mComposerTargets[displayId].valid &&
        mComposerTargets[displayId].security == secure) {
        int32_t index = mComposerTargets[displayId].index;
        if (++index >= mMaxComposerTargetsPerDisplay) {
            index = 0;
        }
        mComposerTargets[displayId].index = index;
        DEBUG_LOG("%s: get pre-allocated %s buffer:%d", __FUNCTION__,
                  secure ? "secure" : "nonsecure", index);
        return std::make_tuple(HWC3::Error::None, mComposerTargets[displayId].handles[index]);
    }
    // security change or display config change, move pervious buffers to mExpiredTargets
    // they will be freed after next framebuffer commited
    if (mComposerTargets.find(displayId) != mComposerTargets.end()) {
        auto& origin = mComposerTargets[displayId].handles;
        std::vector<buffer_handle_t> expired;
        expired.insert(expired.end(), origin.begin(), origin.end());
        mExpiredTargets.emplace(displayId, std::move(expired));
        mComposerTargets.erase(displayId);
    }

    G2dComposerTargets targets;
    uint32_t width, height, format;
    targets.handles.reserve(mMaxComposerTargetsPerDisplay);
    mDisplays[displayId]->getFramebufferInfo(&width, &height, &format);
    auto ret = composer->prepareDeviceFrameBuffer(width, height, format, targets.handles,
                                                  mMaxComposerTargetsPerDisplay, secure);
    if (ret) {
        ALOGE("%s: create framebuffer failed", __FUNCTION__);
        return std::make_tuple(HWC3::Error::NoResources, nullptr);
    }

    targets.index = 0;
    targets.security = secure;
    targets.valid = true;
    mComposerTargets.emplace(displayId, std::move(targets));

    set_g2d_secure_pipe(secure);
    composer->freeSolidColorBuffer();
    // hotplug callback function need device composer to free buffers
    mG2dComposer = std::move(composer);

    return std::make_tuple(HWC3::Error::None, mComposerTargets[displayId].handles[0]);
}

HWC3::Error DrmClient::setSecureMode(int displayId, uint32_t planeId, bool secure) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    int value = secure ? 1 : 0;
    if (mSecureMode.find(displayId) == mSecureMode.end()) {
        if (!mDisplays[displayId]->isSecureDisplay())
            mSecureMode[displayId] = -1;
        else if (mDisplays[displayId]->isSecureEnabled())
            mSecureMode[displayId] = 1;
        else
            mSecureMode[displayId] = 0;
    }
    if ((mSecureMode[displayId] == -1) || (mSecureMode[displayId] == value))
        return HWC3::Error::None;

    mDisplays[displayId]->setSecureMode(mFd, secure);
    mSecureMode[displayId] = value;
    ALOGI("%s: set display %d %s mode", __FUNCTION__, displayId, secure ? "secure" : "nonsecure");

    return HWC3::Error::None;
}

int DrmClient::loadBacklightDevices() {
    struct dirent** dirEntry;
    std::string path("/sys/class/backlight/");
    int count = -1;
    mBacklight.path = "";
    mBacklight.maxBrightness = -1;

    count = scandir(path.c_str(), &dirEntry, 0, alphasort);
    if (count < 0) {
        ALOGE("%s: Cannot find any backlight device in '%s'", __FUNCTION__, path.c_str());
    }
    for (int i = 0; i < count; i++) {
        std::string filePath = path + dirEntry[i]->d_name + "/max_brightness";
        FILE* file = fopen(filePath.c_str(), "r");
        if (!file) {
            free(dirEntry[i]);
            continue;
        }

        char value[5];
        size_t bytesRead = fread(value, 1, 4, file);
        if (bytesRead == 0) {
            ALOGE("%s: Error reading max brightness from %s", __FUNCTION__, filePath.c_str());
            fclose(file);
            free(dirEntry[i]);
            continue;
        } else {
            value[4] = '\0';
            mBacklight.maxBrightness = atoi(value);
            ALOGI("%s: get max brightness=%d from %s", __FUNCTION__, mBacklight.maxBrightness,
                  filePath.c_str());
        }
        mBacklight.path = path + dirEntry[i]->d_name;
        fclose(file);
        for (; i < count; i++) free(dirEntry[i]); // free other dirEntrys
        break;
    }

    if (mBacklight.maxBrightness > 0) {
        // TODO: Here use mDisplayBaseId as primary display Id, and backlight only support primary
        // display
        std::vector<DisplayCapability> caps;
        if (mDisplayCapabilitys.find(mDisplayBaseId) != mDisplayCapabilitys.end())
            caps = mDisplayCapabilitys[mDisplayBaseId];

        caps.push_back(DisplayCapability::BRIGHTNESS);
        mDisplayCapabilitys.emplace(mDisplayBaseId, caps);

        return 1;
    } else {
        return 0;
    }
}

HWC3::Error DrmClient::setBacklightBrightness(int displayId, float brightness) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    int value = (int)(mBacklight.maxBrightness * brightness);
    if ((value == 0) && mDisplays[displayId]->isLowPowerDisplay()) {
        ALOGI("%s: Avoid turning off backlight of low power display in APD side", __FUNCTION__);
        return HWC3::Error::None;
    }
    if (brightness > 1e-5 && value == 0)
        value = 1; // minimum value but not turn off

    DEBUG_LOG("%s: display:%" PRIu32 " adjust brightness=%d(%f)", __FUNCTION__, displayId, value,
              brightness);
    std::string bl = mBacklight.path + "/brightness";
    FILE* file = fopen(bl.c_str(), "w");
    if (!file) {
        ALOGE("%s can not open file %s\n", __FUNCTION__, bl.c_str());
        return HWC3::Error::NoResources;
    }
    fprintf(file, "%d", value);
    fclose(file);

    return HWC3::Error::None;
}

HWC3::Error DrmClient::getDisplayCapability(int displayId, std::vector<DisplayCapability>& caps) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    if (mDisplayCapabilitys.find(displayId) == mDisplayCapabilitys.end()) {
        ALOGW("%s: No display capabilitis in display %" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::NoResources;
    }
    caps.insert(caps.end(), mDisplayCapabilitys[displayId].begin(),
                mDisplayCapabilitys[displayId].end());

    return HWC3::Error::None;
}

HWC3::Error DrmClient::setHdrMetadata(int displayId, hdr_output_metadata* metadata) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    if (mHdrMetadatas.find(displayId) != mHdrMetadatas.end()) {
        if (metadata == NULL) {
            mHdrMetadatas.erase(displayId);
            drmModeDestroyPropertyBlob(mFd.get(), mHdrMetadatas[displayId].blobId);
        } else if (!memcmp(&mHdrMetadatas[displayId].prev, metadata, sizeof(hdr_output_metadata))) {
            DEBUG_LOG("%s: HDR metadata already set, don't need to set again", __FUNCTION__);
            return HWC3::Error::None;
        }
    } else if (metadata == NULL) {
        DEBUG_LOG("%s: No HDR metadata before, don't need to clear again", __FUNCTION__);
        return HWC3::Error::None;
    }

    uint32_t blobId = 0;
    if (metadata != NULL) {
        int ret = drmModeCreatePropertyBlob(mFd.get(), metadata, sizeof(hdr_output_metadata),
                                            &blobId);
        if (ret != 0) {
            ALOGE("%s: Failed to create Metadata blob: %s.", __FUNCTION__, strerror(errno));
            return HWC3::Error::NoResources;
        }
        mHdrMetadatas[displayId].prev = *metadata;
        mHdrMetadatas[displayId].blobId = blobId;
    }

    mDisplays[displayId]->setHdrMetadataBlobId(blobId);

    return HWC3::Error::None;
}

HWC3::Error DrmClient::getDisplayConnectionType(int displayId, DisplayConnectionType* outType) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }
    if (mDisplays[displayId]->isPrimary())
        *outType = DisplayConnectionType::INTERNAL;
    else
        *outType = DisplayConnectionType::EXTERNAL; // TODO: need to set according to actual type

    return HWC3::Error::None;
}

HWC3::Error DrmClient::getDisplayClientTargetProperty(int displayId,
                                                      ClientTargetProperty* outProperty) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    uint32_t width, height, format;
    mDisplays[displayId]->getFramebufferInfo(&width, &height, &format);

    outProperty->pixelFormat = (common::PixelFormat)format;
    outProperty->dataspace = common::Dataspace::SRGB_LINEAR;

    return HWC3::Error::None;
}

using namespace std::chrono_literals;
constexpr auto nsecsPerSec = std::chrono::nanoseconds(1s).count();
HWC3::Error DrmClient::waitVBlank(int displayId, int64_t* timestamp) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    if (!mDisplays[displayId]->isDisplayActive() || !mDisplays[displayId]->isConnected())
        return HWC3::Error::BadDisplay;

    uint32_t high_crtc = (mDisplays[displayId]->getCrtcIndex() << DRM_VBLANK_HIGH_CRTC_SHIFT);
    drmVBlank vblank;
    memset(&vblank, 0, sizeof(vblank));
    vblank.request.type =
            (drmVBlankSeqType)(DRM_VBLANK_RELATIVE | (high_crtc & DRM_VBLANK_HIGH_CRTC_MASK));
    vblank.request.sequence = 1;
    int ret = drmWaitVBlank(mFd, &vblank);
    if (ret) {
        ALOGE("%s wait drm vblank failed", __FUNCTION__);
        return HWC3::Error::NoResources;
    }

    *timestamp =
            (int64_t)vblank.reply.tval_sec * nsecsPerSec + (int64_t)vblank.reply.tval_usec * 1000;

    return HWC3::Error::None;
}
} // namespace aidl::android::hardware::graphics::composer3::impl
