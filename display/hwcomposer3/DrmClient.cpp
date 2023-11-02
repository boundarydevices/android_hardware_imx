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

#include "DrmClient.h"

#include <RWLock.h>
#include <drm_fourcc.h>
#include <gralloc_handle.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "Common.h"
#include "Drm.h"

using android::RWLock;

namespace aidl::android::hardware::graphics::composer3::impl {

DrmClient::~DrmClient() {
    if (mFd > 0) {
        drmDropMaster(mFd.get());
    }
}

HWC3::Error DrmClient::init(char* path, uint32_t* baseId) {
    DEBUG_LOG("%s", __FUNCTION__);

    mFd = ::android::base::unique_fd(open(path /*"/dev/dri/card0"*/, O_RDWR | O_CLOEXEC));
    if (mFd < 0) {
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
    }

    {
        ::android::RWLock::AutoWLock lock(mDisplaysMutex);
        bool success = loadDrmDisplays(displayBaseId);
        if (success) {
            DEBUG_LOG("%s: Successfully initialized DRM backend", __FUNCTION__);
        } else {
            ALOGE("%s: Failed to initialize DRM backend", __FUNCTION__);
            return HWC3::Error::NoResources;
        }
    }

    constexpr const std::size_t kCachedBuffersPerDisplay = 10;
    std::size_t numDisplays = mDisplays.size();
    const std::size_t bufferCacheSize = kCachedBuffersPerDisplay * numDisplays;
    DEBUG_LOG("%s: initializing DRM buffer cache to size %zu", __FUNCTION__, bufferCacheSize);
    mBufferCache = std::make_unique<DrmBufferCache>(bufferCacheSize);

    mDrmEventListener = DrmEventListener::create(mFd, [this]() { handleHotplug(); });
    if (!mDrmEventListener) {
        ALOGE("%s: Failed to initialize DRM event listener", __FUNCTION__);
    } else {
        DEBUG_LOG("%s: Successfully initialized DRM event listener", __FUNCTION__);
    }

    *baseId = displayBaseId;
    mDisplayBaseId = displayBaseId;
    DEBUG_LOG("%s: Successfully initialized.", __FUNCTION__);
    return HWC3::Error::None;
}

HWC3::Error DrmClient::getDisplayConfigs(std::vector<HalMultiConfigs>* configs) const {
    DEBUG_LOG("%s", __FUNCTION__);

    ::android::RWLock::AutoRLock lock(mDisplaysMutex);

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

        auto check_fun = [&](std::unique_ptr<DrmPlane>& plane) -> bool {
            if (!plane->isOverlay() && !plane->isPrimary()) {
                return false;
            }
            if (plane->isCompatibleWith(*crtc)) {
                crtc_planes.insert({plane->getId(), std::move(plane)});
                return true;
            } else {
                return false;
            }
        };
        auto it = std::find_if(planes.begin(), planes.end(), check_fun);
        while (it != planes.end()) {
            // it = std::find_if(it + 1, planes.end(), check_fun);
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

    return true;
}

std::tuple<HWC3::Error, std::shared_ptr<DrmBuffer>> DrmClient::create(const native_handle_t* handle,
                                                                      common::Rect displayFrame,
                                                                      common::Rect sourceCrop) {
    gralloc_handle_t memHandle = (gralloc_handle_t)handle;
    if (memHandle == nullptr) {
        ALOGE("%s: invalid gralloc_handle", __FUNCTION__);
        return std::make_tuple(HWC3::Error::NoResources, nullptr);
    }

    DrmPrimeBufferHandle primeHandle = 0;
    int ret = drmPrimeFDToHandle(mFd.get(), memHandle->fd, &primeHandle);
    if (ret) {
        ALOGE("%s: drmPrimeFDToHandle failed: %s (errno %d)", __FUNCTION__, strerror(errno), errno);
        return std::make_tuple(HWC3::Error::NoResources, nullptr);
    }

    auto drmBufferPtr = mBufferCache->get(primeHandle);
    if (drmBufferPtr != nullptr) {
        DEBUG_LOG("%s: found framebuffer:%" PRIu32, __FUNCTION__,
                  *(*drmBufferPtr)->mDrmFramebuffer);
        return std::make_tuple(HWC3::Error::None, std::shared_ptr<DrmBuffer>(*drmBufferPtr));
    }

    uint64_t modifier;
    auto buffer = std::shared_ptr<DrmBuffer>(new DrmBuffer(*this));
    buffer->mWidth = memHandle->width;
    buffer->mHeight = memHandle->height;
    buffer->mDisplayFrame = displayFrame;
    buffer->mSourceCrop = sourceCrop;
    buffer->mDrmFormat = ConvertNxpFormatToDrmFormat(memHandle->fslFormat, &modifier);
    buffer->mPlaneFds[0] = memHandle->fd;
    for (uint32_t i = 0; i < memHandle->num_planes; i++) {
        buffer->mPlaneHandles[i] = primeHandle;
        buffer->mPlanePitches[i] = memHandle->strides[i];
        buffer->mPlaneOffsets[i] = memHandle->offsets[i];
        if (memHandle->format_modifier > 0)
            buffer->mPlaneModifiers[i] =
                    memHandle->format_modifier; // modifier of framebuffer is setted when allocate.
        else if (modifier > 0)
            buffer->mPlaneModifiers[i] = modifier;
    }

    // buffer->mMeta =
    // MemoryManager::getInstance()->getMetaData(const_cast<gralloc_handle_t>(memHandle));

    uint32_t framebuffer = 0;
    uint32_t format = buffer->mDrmFormat;
    if (memHandle->format_modifier > 0) { // TODO: some workaround for framebuffer
        /* workaround GPU SUPER_TILED R/B swap issue, for no-resolve and tiled output
           GPU not distinguish A8B8G8R8 and A8R8G8B8, all regard as A8R8G8B8, need do
           R/B swap here for no-resolve and tiled buffer */
        if (format == DRM_FORMAT_XBGR8888)
            format = DRM_FORMAT_XRGB8888;
        if (format == DRM_FORMAT_ABGR8888)
            format = DRM_FORMAT_ARGB8888;
    }

    if (buffer->mPlaneModifiers[0] > 0) {
        ret = drmModeAddFB2WithModifiers(mFd.get(), buffer->mWidth, buffer->mHeight, format,
                                         buffer->mPlaneHandles, buffer->mPlanePitches,
                                         buffer->mPlaneOffsets, buffer->mPlaneModifiers,
                                         &framebuffer, DRM_MODE_FB_MODIFIERS);
    } else {
        /* IMX8MQ mxsfb driver require buffer pitches == width * format_cpp,
           so here buffer width use stride directly. */
        ret = drmModeAddFB2(mFd.get(), memHandle->stride, buffer->mHeight, buffer->mDrmFormat,
                            buffer->mPlaneHandles, buffer->mPlanePitches, buffer->mPlaneOffsets,
                            &framebuffer, 0);
    }
    if (ret) {
        ALOGE("%s: drmModeAddFB2 failed: %s (errno %d)", __FUNCTION__, strerror(errno), errno);
        return std::make_tuple(HWC3::Error::NoResources, nullptr);
    }
    DEBUG_LOG("%s: created framebuffer:%" PRIu32, __FUNCTION__, framebuffer);
    buffer->mDrmFramebuffer = framebuffer;

    mBufferCache->set(primeHandle, std::shared_ptr<DrmBuffer>(buffer));

    return std::make_tuple(HWC3::Error::None, std::shared_ptr<DrmBuffer>(buffer));
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

        mBufferCache->remove(buffer->mPlaneHandles[0]);
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

    {
        ::android::RWLock::AutoWLock lock(mDisplaysMutex);

        for (auto& pair : mDisplays) {
            DrmDisplay* display = pair.second.get();
            auto change = display->checkAndHandleHotplug(mFd);
            if (change == DrmHotplugChange::kNoChange) {
                continue;
            }

            if ((change == DrmHotplugChange::kDisconnected) && display->isPrimary()) {
                // primary display cannot be disconnected when report
                // power off when disconnected (for dcss of imx8mq)
                change = DrmHotplugChange::kConnected;
                display->setPowerMode(mFd, DrmPower::kPowerOff);
                display->placeholderDisplayConfigs();
                ALOGW("primary display cannot hotplug");
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
    if (!mDisplays[displayId]->isConnected()) {
        ALOGI("%s: %d display is disconnected, avoid DRM committing", __FUNCTION__, displayId);
        return std::make_tuple(HWC3::Error::None, ::android::base::unique_fd());
    }

    ::android::RWLock::AutoRLock lock(mDisplaysMutex);
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

    return mDisplays[displayId]->commit(std::move(request), mFd);
}

std::optional<std::vector<uint8_t>> DrmClient::getEdid(uint32_t displayId) {
    ::android::RWLock::AutoRLock lock(mDisplaysMutex);

    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return std::nullopt;
    }

    return mDisplays[displayId]->getEdid();
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

HWC3::Error DrmClient::prepareDrmPlanesForValidate(int displayId) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    mDisplays[displayId]->buildPlaneIdPool();

    return HWC3::Error::None;
}

std::tuple<HWC3::Error, uint32_t> DrmClient::getPlaneForLayerBuffer(int displayId,
                                                                    const native_handle_t* handle) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return std::make_tuple(HWC3::Error::BadDisplay, 0);
    }

    uint32_t planeId = mDisplays[displayId]->findDrmPlane(handle);
    if (planeId > 0) {
        gralloc_handle_t memHandle = (gralloc_handle_t)handle;
        DEBUG_LOG("%s: display:%" PRIu32 " find plane=%d for buffer:%s", __FUNCTION__, displayId,
                  planeId, memHandle->name);
        return std::make_tuple(HWC3::Error::None, planeId);
    } else {
        gralloc_handle_t memHandle = (gralloc_handle_t)handle;
        DEBUG_LOG("%s: display:%" PRIu32 " NOT find plane for buffer:%s", __FUNCTION__, displayId,
                  memHandle->name);
        return std::make_tuple(HWC3::Error::NoResources, 0);
    }
}

HWC3::Error DrmClient::setPrimaryDisplay(int displayId) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    mDisplays[displayId]->setAsPrimary(true);

    return HWC3::Error::None;
}

HWC3::Error DrmClient::fakeDisplayConfig(int displayId) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    mDisplays[displayId]->placeholderDisplayConfigs();

    return HWC3::Error::None;
}

std::tuple<HWC3::Error, buffer_handle_t> DrmClient::getComposerTarget(DeviceComposer* composer,
                                                                      int displayId) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return std::make_tuple(HWC3::Error::BadDisplay, nullptr);
    }

    if (mComposerTargets.find(displayId) != mComposerTargets.end()) {
        int32_t index = mTargetIndex[displayId];
        if (++index >= MAX_COMPOSER_TARGETS_PER_DISPLAY) {
            index = 0;
        }
        mTargetIndex[displayId] = index;
        DEBUG_LOG("%s: get pre-allocated buffer:%d", __FUNCTION__, index);
        return std::make_tuple(HWC3::Error::None, mComposerTargets[displayId][index]);
    }

    gralloc_handle_t bufferHandles[MAX_COMPOSER_TARGETS_PER_DISPLAY];
    auto ret = mDisplays[displayId]->createDeviceFramebuffer(composer, bufferHandles,
                                                             MAX_COMPOSER_TARGETS_PER_DISPLAY);
    if (ret) {
        ALOGE("%s: create framebuffer failed", __FUNCTION__);
        return std::make_tuple(HWC3::Error::NoResources, nullptr);
    }

    std::vector<gralloc_handle_t> buffers;
    for (int i = 0; i < MAX_COMPOSER_TARGETS_PER_DISPLAY; i++) buffers.push_back(bufferHandles[i]);

    mComposerTargets.emplace(displayId, buffers);
    mTargetIndex.emplace(displayId, 0);

    return std::make_tuple(HWC3::Error::None, mComposerTargets[displayId][0]);
}

} // namespace aidl::android::hardware::graphics::composer3::impl