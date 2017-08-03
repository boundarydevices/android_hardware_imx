/*
 * Copyright 2017 NXP.
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

#include <inttypes.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <cutils/log.h>
#include <sync/sync.h>
#include <cutils/properties.h>

#include <linux/fb.h>
#include <linux/mxcfb.h>

#include "Memory.h"
#include "MemoryManager.h"
#include "KmsDisplay.h"

namespace fsl {

KmsDisplay::KmsDisplay()
{
    mDrmFd = -1;
    mVsyncThread = NULL;
    mTargetIndex = 0;
    memset(&mTargets[0], 0, sizeof(mTargets));
    mIonManager = new IonManager();
    mModeset = true;
    memset(&mKmsProperty, 0, sizeof(mKmsProperty));
    mConnectorID = 0;
}

KmsDisplay::~KmsDisplay()
{
    sp<VSyncThread> vsync = NULL;
    {
        Mutex::Autolock _l(mLock);
        vsync = mVsyncThread;
    }

    if (vsync != NULL) {
        vsync->requestExit();
    }

    closeKms();
    if (mDrmFd > 0) {
        close(mDrmFd);
    }
}

/*
 * Find the property IDs and value that match its name.
 */
void KmsDisplay::getPropertyValue(uint32_t objectID, uint32_t objectType,
                          const char *propName, uint32_t* propId,
                          uint64_t* value)
{
    int found = 0;
    uint64_t ivalue = 0;
    uint32_t id = 0;

    drmModeObjectPropertiesPtr pModeObjectProperties =
        drmModeObjectGetProperties(mDrmFd, objectID, objectType);

    if (pModeObjectProperties == NULL) {
        ALOGE("drmModeObjectGetProperties failed.");
        return;
    }

    for (uint32_t i = 0; i < pModeObjectProperties->count_props; i++) {
        drmModePropertyPtr pProperty =
            drmModeGetProperty(mDrmFd, pModeObjectProperties->props[i]);
        if (pProperty == NULL) {
            ALOGE("drmModeGetProperty failed.");
            continue;
        }

        ALOGV("prop input name:%s, actual name:%s", propName, pProperty->name);
        if (strcmp(propName, pProperty->name) == 0) {
            ivalue = pModeObjectProperties->prop_values[i];
            id = pProperty->prop_id;
            drmModeFreeProperty(pProperty);
            break;
        }

        drmModeFreeProperty(pProperty);
    }

    drmModeFreeObjectProperties(pModeObjectProperties);

    if (propId != NULL) {
        *propId = id;
    }

    if (value != NULL) {
        *value = ivalue;
    }
}

void KmsDisplay::getTableProperty(uint32_t objectID,
                                  uint32_t objectType,
                                  struct TableProperty *table,
                                  size_t tableLen)
{
    for (uint32_t i = 0; i < tableLen; i++) {
        getPropertyValue(objectID, objectType, table[i].name, table[i].ptr, NULL);
        if (*(table[i].ptr) == 0) {
            ALOGE("can't find property ID for \'%s\'.", table[i].name);
        }
    }
}

/*
 * Find the property IDs in group with type.
 */
void KmsDisplay::getKmsProperty()
{
    struct TableProperty crtcTable[] = {
        {"MODE_ID", &mKmsProperty.crtc.mode_id},
        {"ACTIVE",  &mKmsProperty.crtc.active},
    };

    struct TableProperty planeTable[] = {
        {"SRC_X",   &mKmsProperty.plane.src_x},
        {"SRC_Y",   &mKmsProperty.plane.src_y},
        {"SRC_W",   &mKmsProperty.plane.src_w},
        {"SRC_H",   &mKmsProperty.plane.src_h},
        {"CRTC_X",  &mKmsProperty.plane.crtc_x},
        {"CRTC_Y",  &mKmsProperty.plane.crtc_y},
        {"CRTC_W",  &mKmsProperty.plane.crtc_w},
        {"CRTC_H",  &mKmsProperty.plane.crtc_h},
        {"FB_ID",   &mKmsProperty.plane.fb_id},
        {"CRTC_ID", &mKmsProperty.plane.crtc_id},
    };

    struct TableProperty connectorTable[] = {
        {"CRTC_ID", &mKmsProperty.connector.crtc_id},
        {"DPMS", &mKmsProperty.connector.dpms_id},
    };

    getTableProperty(mCrtcID,
                     DRM_MODE_OBJECT_CRTC,
                     crtcTable, ARRAY_LEN(crtcTable));
    getTableProperty(mPlaneID,
                     DRM_MODE_OBJECT_PLANE,
                     planeTable, ARRAY_LEN(planeTable));
    getTableProperty(mConnectorID,
                     DRM_MODE_OBJECT_CONNECTOR,
                     connectorTable, ARRAY_LEN(connectorTable));
}

/*
 * add properties to a drmModeAtomicReqPtr object.
 */
void KmsDisplay::addAtomicRequest(drmModeAtomicReqPtr pset,
                                     uint32_t modeID, uint32_t fb)
{
    /* Specify the mode to use on the CRTC, and make the CRTC active. */
    if (mModeset) {
        drmModeAtomicAddProperty(pset, mCrtcID,
                             mKmsProperty.crtc.mode_id, modeID);
        drmModeAtomicAddProperty(pset, mCrtcID,
                             mKmsProperty.crtc.active, 1);

        /* Tell the connector to receive pixels from the CRTC. */
        drmModeAtomicAddProperty(pset, mConnectorID,
                             mKmsProperty.connector.crtc_id, mCrtcID);
    }

    /*
     * Specify the surface to display in the plane, and connect the
     * plane to the CRTC.
     */
    drmModeAtomicAddProperty(pset, mPlaneID,
                             mKmsProperty.plane.fb_id, fb);
    drmModeAtomicAddProperty(pset, mPlaneID,
                             mKmsProperty.plane.crtc_id, mCrtcID);

    /*
     * Specify the region of source surface to display (i.e., the
     * "ViewPortIn").  Note these values are in 16.16 format, so shift
     * up by 16.
     */
    const DisplayConfig& config = mConfigs[mActiveConfig];
    drmModeAtomicAddProperty(pset, mPlaneID,
                             mKmsProperty.plane.src_x, 0);
    drmModeAtomicAddProperty(pset, mPlaneID,
                             mKmsProperty.plane.src_y, 0);
    drmModeAtomicAddProperty(pset, mPlaneID,
                             mKmsProperty.plane.src_w, config.mXres << 16);
    drmModeAtomicAddProperty(pset, mPlaneID,
                             mKmsProperty.plane.src_h, config.mYres << 16);

    /*
     * Specify the region within the mode where the image should be
     * displayed (i.e., the "ViewPortOut").
     */
    drmModeAtomicAddProperty(pset, mPlaneID,
                             mKmsProperty.plane.crtc_x, 0);
    drmModeAtomicAddProperty(pset, mPlaneID,
                             mKmsProperty.plane.crtc_y, 0);
    drmModeAtomicAddProperty(pset, mPlaneID,
                             mKmsProperty.plane.crtc_w, config.mXres);
    drmModeAtomicAddProperty(pset, mPlaneID,
                             mKmsProperty.plane.crtc_h, config.mYres);
}

int KmsDisplay::setPowerMode(int mode)
{
    Mutex::Autolock _l(mLock);

    switch (mode) {
        case POWER_ON:
            mPowerMode = DRM_MODE_DPMS_ON;
            break;
        case POWER_DOZE:
            mPowerMode = DRM_MODE_DPMS_STANDBY;
            break;
        case POWER_DOZE_SUSPEND:
            mPowerMode = DRM_MODE_DPMS_SUSPEND;
            break;
        case POWER_OFF:
            mPowerMode = DRM_MODE_DPMS_OFF;
            break;
        default:
            mPowerMode = DRM_MODE_DPMS_ON;
            break;
    }

    int err = drmModeConnectorSetProperty(mDrmFd, mConnectorID,
                  mKmsProperty.connector.dpms_id, mPowerMode);
    if (err != 0) {
        ALOGE("failed to set DPMS mode");
    }

    return err;
}

void KmsDisplay::enableVsync()
{
    Mutex::Autolock _l(mLock);
    if (mDrmFd < 0) {
        return;
    }

    mVsyncThread = new VSyncThread(this);
}

void KmsDisplay::setCallback(EventListener* callback)
{
    Mutex::Autolock _l(mLock);
    mListener = callback;
}

void KmsDisplay::setVsyncEnabled(bool enabled)
{
    sp<VSyncThread> vsync = NULL;
    {
        Mutex::Autolock _l(mLock);
        vsync = mVsyncThread;
    }

    if (vsync != NULL) {
        vsync->setEnabled(enabled);
    }
}

void KmsDisplay::setFakeVSync(bool enable)
{
    sp<VSyncThread> vsync = NULL;
    {
        Mutex::Autolock _l(mLock);
        vsync = mVsyncThread;
    }

    if (vsync != NULL) {
        vsync->setFakeVSync(enable);
    }
}

int KmsDisplay::onMemoryDestroyed(Memory* handle)
{
    if (handle == NULL) {
        return 0;
    }

    if (handle->fbId != 0) {
        drmModeRmFB(mDrmFd, handle->fbId);
    }

    if (handle->gemHandle != 0) {
        struct drm_gem_close gem_close;
        memset(&gem_close, 0, sizeof(gem_close));
        gem_close.handle = handle->gemHandle;
        drmIoctl(mDrmFd, DRM_IOCTL_GEM_CLOSE, &gem_close);
    }

    return 0;
}

int KmsDisplay::updateScreen()
{
    int drmfd = -1;
    Memory* buffer = NULL;
    {
        Mutex::Autolock _l(mLock);

        if (!mConnected) {
            ALOGE("updateScreen display plugout");
            return -EINVAL;
        }

        if (mPowerMode != POWER_ON) {
            ALOGE("can't update screen power mode:%d", mPowerMode);
            return -EINVAL;
        }
        buffer = mRenderTarget;
        drmfd = mDrmFd;
    }

    if (drmfd < 0) {
        ALOGE("%s invalid drmfd", __func__);
        return -EINVAL;
    }

    if (!buffer || !(buffer->flags & FLAGS_FRAMEBUFFER)) {
        ALOGE("%s buffer is invalid", __func__);
        return -EINVAL;
    }

    const DisplayConfig& config = mConfigs[mActiveConfig];
    if (buffer->fbId == 0) {
        int format = convertFormatToDrm(buffer->fslFormat);
        int stride = buffer->stride * config.mBytespixel;
        uint32_t bo_handles[4] = {0};
        uint32_t pitches[4] = {0};
        uint32_t offsets[4] = {0};

        bo_handles[0] = buffer->gemHandle;
        pitches[0] = stride;
        drmPrimeFDToHandle(mDrmFd, buffer->fd, (uint32_t*)&buffer->gemHandle);
        drmModeAddFB2(mDrmFd, buffer->width, buffer->height, format,
                    bo_handles, pitches, offsets, (uint32_t*)&buffer->fbId, 0);
        MemoryShadow* shadow = (MemoryShadow*)(uintptr_t)buffer->shadow;
        if (shadow != NULL) {
            shadow->setListener(this);
        }
    }

    if (buffer->fbId == 0) {
        return 0;
    }

    drmModeAtomicReqPtr pset = drmModeAtomicAlloc();
    if (!pset) {
        ALOGE("Failed to allocate property set");
        return -ENOMEM;
    }

    uint32_t modeID = 0;
    uint32_t flags = DRM_MODE_ATOMIC_NONBLOCK;
    if (mModeset) {
        flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
        drmModeCreatePropertyBlob(drmfd, &mMode, sizeof(mMode), &modeID);
    }

    addAtomicRequest(pset, modeID, buffer->fbId);
    int ret = drmModeAtomicCommit(drmfd, pset, flags, NULL /* user_data */);
    if (ret != 0) {
        ALOGV("Failed to commit pset ret=%d", ret);
    }

    drmModeAtomicFree(pset);
    if (mModeset) {
        mModeset = false;
        drmModeDestroyPropertyBlob(drmfd, modeID);
    }

    return 0;
}

int KmsDisplay::openKms(drmModeResPtr pModeRes)
{
    Mutex::Autolock _l(mLock);

    if (mDrmFd < 0 || mConnectorID == 0) {
        ALOGE("%s invalid drmfd or connector id", __func__);
        return -ENODEV;
    }

    int ret = drmSetClientCap(mDrmFd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    if (ret) {
        ALOGE("failed to set universal plane cap %d", ret);
        return ret;
    }

    ret = drmSetClientCap(mDrmFd, DRM_CLIENT_CAP_ATOMIC, 1);
    if (ret) {
        ALOGE("failed to set atomic cap %d", ret);
        return ret;
    }

    drmModeConnectorPtr pConnector = drmModeGetConnector(mDrmFd, mConnectorID);
    if (pConnector == NULL) {
        ALOGE("%s drmModeGetConnector failed for "
              "connector index %d", __func__, mConnectorID);
        return -ENODEV;
    }

    drmModeEncoderPtr pEncoder =
        drmModeGetEncoder(mDrmFd, pConnector->encoders[0]);

    if (pEncoder == NULL) {
        ALOGE("drmModeGetEncoder failed for"
              "encoder 0x%08x", pConnector->encoders[0]);
        return -ENODEV;
    }

    int index = findBestMatch(pConnector);
    mMode = pConnector->modes[index];
    for (int i = 0; i < pModeRes->count_crtcs; i++) {
        if ((pEncoder->possible_crtcs & (1 << i)) == 0) {
            continue;
        }

        mCrtcID = pModeRes->crtcs[i];
        mCrtcIndex = i;
        break;
    }

    if (mCrtcID == 0) {
        ALOGE("can't get valid CRTC.");
        return -ENODEV;
    }

    drmModeFreeEncoder(pEncoder);
    getPrimaryPlane();
    getKmsProperty();

    ssize_t configId = getConfigIdLocked(mMode.hdisplay, mMode.vdisplay);
    if (configId < 0) {
        ALOGE("can't find config: w:%d, h:%d", mMode.hdisplay, mMode.vdisplay);
        return -1;
    }

    DisplayConfig& config = mConfigs.editItemAt(configId);
    config.mXdpi = mMode.hdisplay * 25400 / pConnector->mmWidth;
    config.mYdpi = mMode.vdisplay * 25400 / pConnector->mmHeight;
    config.mFps  = mMode.vrefresh;
    config.mVsyncPeriod  = 1000000000 / mMode.vrefresh;
    config.mFormat = FORMAT_RGBA8888;
    config.mBytespixel = 4;
    ALOGW("xres         = %d px\n"
          "yres         = %d px\n"
          "xdpi         = %.2f ppi\n"
          "ydpi         = %.2f ppi\n"
          "fps          = %.2f Hz\n",
          config.mXres, config.mYres, config.mXdpi / 1000.0f,
          config.mYdpi / 1000.0f, config.mFps);

    if (pConnector != NULL) {
        drmModeFreeConnector(pConnector);
    }

    mActiveConfig = configId;
    prepareTargetsLocked();

    return 0;
}

int KmsDisplay::findBestMatch(drmModeConnectorPtr pConnector)
{
    int index = 0;
    unsigned int delta = -1, rdelta = -1;
    char value[PROPERTY_VALUE_MAX];
    int width = 0, height = 0;

    memset(value, 0, sizeof(value));
    property_get("ro.boot.displaymode", value, "1080p");
    if (!strncmp(value, "2k", 2)) {
        width = 2048;
    }
    else if (!strncmp(value, "4k", 2)) {
        width = 4096;
    }
    else {
        height = atoi(value);
        if (height == 0) {
            height = 1080;
        }
    }
    ALOGV("%s mode:%s, width:%d, height:%d", __func__, value, width, height);

    for (int i=0; i<pConnector->count_modes; i++) {
        drmModeModeInfo mode = pConnector->modes[i];
        if (mode.vrefresh < 60) {
            // bypass fps < 60.
            continue;
        }
        ALOGV("mode[%d]: w:%d, h:%d", i, mode.hdisplay, mode.vdisplay);

        if (height > 0) {
            rdelta = abs((height - mode.vdisplay));
        }
        else {
            rdelta = abs((width - mode.hdisplay));
        }

        if (rdelta < delta) {
            delta = rdelta;
            index = i;
        }

        if (delta == 0) {
            break;
        }
    }

    drmModeModeInfo mode = pConnector->modes[index];
    ALOGV("find best mode w:%d, h:%d", mode.hdisplay, mode.vdisplay);

    return index;
}

int KmsDisplay::getPrimaryPlane()
{
    drmModePlaneResPtr pPlaneRes = drmModeGetPlaneResources(mDrmFd);
    if (pPlaneRes == NULL) {
        ALOGE("drmModeGetPlaneResources failed");
        return -ENODEV;
    }

    for (size_t i = 0; i < pPlaneRes->count_planes; i++) {
        drmModePlanePtr pPlane = drmModeGetPlane(mDrmFd, pPlaneRes->planes[i]);
        uint32_t crtcs;
        uint64_t type;

        if (pPlane == NULL) {
            ALOGE("drmModeGetPlane failed for plane %zu", i);
            continue;
        }

        crtcs = pPlane->possible_crtcs;

        for (size_t k=0; k<pPlane->count_formats; k++) {
            uint32_t nFormat = pPlane->formats[k];
            ALOGV("available format: %c%c%c%c", nFormat&0xFF, (nFormat>>8)&0xFF,
                            (nFormat>>16)&0xFF, (nFormat>>24)&0xFF);
        }

        drmModeFreePlane(pPlane);

        if ((crtcs & (1 << mCrtcIndex)) == 0) {
            continue;
        }

        getPropertyValue(pPlaneRes->planes[i],
                         DRM_MODE_OBJECT_PLANE,
                        "type", NULL, &type);

        if (type == DRM_PLANE_TYPE_PRIMARY) {
            mPlaneID = pPlaneRes->planes[i];
            break;
        }
    }

    drmModeFreePlaneResources(pPlaneRes);

    if (mPlaneID == 0) {
        ALOGE("can't find primary plane.");
        return -ENODEV;
    }

    return 0;
}

int KmsDisplay::closeKms()
{
    ALOGV("close kms");
    invalidLayers();

    Mutex::Autolock _l(mLock);

    mRenderTarget = NULL;
    if (mAcquireFence != -1) {
        close(mAcquireFence);
        mAcquireFence = -1;
    }
    mConfigs.clear();
    mActiveConfig = -1;

    releaseTargetsLocked();
    return 0;
}

uint32_t KmsDisplay::convertFormatToDrm(uint32_t format)
{
    switch (format) {
        case FORMAT_RGB888:
            return DRM_FORMAT_BGR888;
        case FORMAT_BGRA8888:
            return DRM_FORMAT_ARGB8888;
        case FORMAT_RGBX8888:
            return DRM_FORMAT_XBGR8888;
        case FORMAT_RGBA8888:
            //return DRM_FORMAT_ABGR8888;
            return DRM_FORMAT_XBGR8888;
        case FORMAT_RGB565:
            return DRM_FORMAT_BGR565;
        case FORMAT_NV12:
            return DRM_FORMAT_NV12;
        case FORMAT_NV21:
            return DRM_FORMAT_NV21;
        case FORMAT_I420:
            return DRM_FORMAT_YUV420;
        case FORMAT_YV12:
            return DRM_FORMAT_YVU420;
        case FORMAT_NV16:
            return DRM_FORMAT_NV16;
        case FORMAT_YUYV:
            return DRM_FORMAT_YUYV;
        default:
            ALOGE("Cannot convert format to drm %u", format);
            return -EINVAL;
    }

    return -EINVAL;
}

void KmsDisplay::prepareTargetsLocked()
{
    if (!mComposer.isValid()) {
        ALOGI("no need to alloc memory");
        return;
    }

    MemoryDesc desc;
    const DisplayConfig& config = mConfigs[mActiveConfig];
    desc.mWidth = config.mXres;
    desc.mHeight = config.mYres;
    desc.mFormat = config.mFormat;
    desc.mFslFormat = config.mFormat;
    desc.mProduceUsage |= USAGE_HW_COMPOSER |
                          USAGE_HW_2D | USAGE_HW_RENDER;
    desc.mFlag = FLAGS_FRAMEBUFFER;
    desc.checkFormat();

    for (int i=0; i<MAX_FRAMEBUFFERS; i++) {
        mIonManager->allocMemory(desc, &mTargets[i]);
    }
    mTargetIndex = 0;
}

void KmsDisplay::releaseTargetsLocked()
{
    for (int i=0; i<MAX_FRAMEBUFFERS; i++) {
        if (mTargets[i] == NULL) {
            continue;
        }
        mIonManager->releaseMemory(mTargets[i]);
        mTargets[i] = NULL;
    }
    mTargetIndex = 0;
}

int KmsDisplay::getConfigIdLocked(int width, int height)
{
    int index = -1;
    DisplayConfig config;
    config.mXres = width;
    config.mYres = height;

    index = mConfigs.indexOf(config);
    if (index < 0) {
        index = mConfigs.add(config);
    }

    return index;
}

int KmsDisplay::setActiveConfig(int configId)
{
    Mutex::Autolock _l(mLock);
    if (mActiveConfig == configId) {
        ALOGI("the same config, no need to change");
        return 0;
    }

    if (configId < 0 || configId >= (int)mConfigs.size()) {
        ALOGI("invalid config id:%d", configId);
        return -EINVAL;
    }

    releaseTargetsLocked();
    prepareTargetsLocked();

    return 0;
}

int KmsDisplay::composeLayers()
{
    Mutex::Autolock _l(mLock);

    // mLayerVector's size > 0 means 2D composite.
    // only this case needs override mRenderTarget.
    if (mLayerVector.size() > 0) {
        mTargetIndex = mTargetIndex % MAX_FRAMEBUFFERS;
        mRenderTarget = mTargets[mTargetIndex];
        mTargetIndex++;
    }

    return composeLayersLocked();
}

void KmsDisplay::handleVsyncEvent(nsecs_t timestamp)
{
    EventListener* callback = NULL;
    {
        Mutex::Autolock _l(mLock);
        callback = mListener;
    }

    if (callback == NULL) {
        return;
    }

    callback->onVSync(DISPLAY_PRIMARY, timestamp);
}

int KmsDisplay::setDrm(int drmfd, size_t connectorId)
{
    if (drmfd < 0 || connectorId == 0) {
        ALOGE("%s invalid drmfd or connector id", __func__);
        return -ENODEV;
    }

    Mutex::Autolock _l(mLock);
    if (mDrmFd > 0) {
        close(mDrmFd);
    }
    mDrmFd = dup(drmfd);
    mConnectorID = connectorId;

    return 0;
}

int KmsDisplay::powerMode()
{
    Mutex::Autolock _l(mLock);
    return mPowerMode;
}

int KmsDisplay::readType()
{
    if (mDrmFd < 0 || mConnectorID == 0) {
        ALOGE("%s invalid drmfd or connector id", __func__);
        return -ENODEV;
    }

    drmModeConnectorPtr pConnector = drmModeGetConnector(mDrmFd, mConnectorID);
    if (pConnector == NULL) {
        ALOGE("%s drmModeGetConnector failed for "
              "connector index %d", __func__, mConnectorID);
        return -ENODEV;
    }

    switch (pConnector->connector_type) {
        case DRM_MODE_CONNECTOR_LVDS:
            mType = DISPLAY_LDB;
            break;
        case DRM_MODE_CONNECTOR_HDMIA:
        case DRM_MODE_CONNECTOR_HDMIB:
        case DRM_MODE_CONNECTOR_TV:
            mType = DISPLAY_HDMI;
            break;
        case DRM_MODE_CONNECTOR_DVII:
        case DRM_MODE_CONNECTOR_DVID:
        case DRM_MODE_CONNECTOR_DVIA:
            mType = DISPLAY_DVI;
            break;
        default:
            mType = DISPLAY_LDB;
            ALOGI("no support display type:%d", pConnector->connector_type);
            break;
    }

    if (pConnector != NULL) {
        drmModeFreeConnector(pConnector);
    }

    return 0;
}

int KmsDisplay::readConnection()
{
    if (mDrmFd < 0 || mConnectorID == 0) {
        ALOGE("%s invalid drmfd or connector id", __func__);
        return -ENODEV;
    }

    drmModeConnectorPtr pConnector = drmModeGetConnector(mDrmFd, mConnectorID);
    if (pConnector == NULL) {
        ALOGE("%s drmModeGetConnector failed for "
              "connector index %d", __func__, mConnectorID);
        return -ENODEV;
    }

    if ((pConnector->connection == DRM_MODE_CONNECTED) &&
        (pConnector->count_modes > 0) &&
        (pConnector->count_encoders > 0)) {
        mConnected = true;
    }
    else {
        mConnected = false;
    }

    if (pConnector != NULL) {
        drmModeFreeConnector(pConnector);
    }

    return 0;
}

//----------------------------------------------------------
extern "C" int clock_nanosleep(clockid_t clock_id, int flags,
                           const struct timespec *request,
                           struct timespec *remain);

KmsDisplay::VSyncThread::VSyncThread(KmsDisplay *ctx)
    : Thread(false), mCtx(ctx), mEnabled(false),
      mFakeVSync(false), mNextFakeVSync(0)
{
    mRefreshPeriod = 0;
}

void KmsDisplay::VSyncThread::onFirstRef()
{
    run("HWC-VSYNC-Thread", android::PRIORITY_URGENT_DISPLAY);
}

int32_t KmsDisplay::VSyncThread::readyToRun()
{
    return 0;
}

void KmsDisplay::VSyncThread::setEnabled(bool enabled) {
    Mutex::Autolock _l(mLock);
    mEnabled = enabled;
    mCondition.signal();
}

void KmsDisplay::VSyncThread::setFakeVSync(bool enable)
{
    Mutex::Autolock _l(mLock);
    mFakeVSync = enable;
}

bool KmsDisplay::VSyncThread::threadLoop()
{
    { // scope for lock
        Mutex::Autolock _l(mLock);
        while (!mEnabled) {
            mCondition.wait(mLock);
        }
    }

    if (mFakeVSync) {
        performFakeVSync();
    }
    else {
        performVSync();
    }

    return true;
}

void KmsDisplay::VSyncThread::performFakeVSync()
{
    const DisplayConfig& config = mCtx->getActiveConfig();
    mRefreshPeriod = config.mVsyncPeriod;
    const nsecs_t period = mRefreshPeriod;
    const nsecs_t now = systemTime(CLOCK_MONOTONIC);
    nsecs_t next_vsync = mNextFakeVSync;
    nsecs_t sleep = next_vsync - now;
    if (sleep < 0) {
        // we missed, find where the next vsync should be
        sleep = (period - ((now - next_vsync) % period));
        next_vsync = now + sleep;
    }
    mNextFakeVSync = next_vsync + period;

    struct timespec spec;
    spec.tv_sec  = next_vsync / 1000000000;
    spec.tv_nsec = next_vsync % 1000000000;

    int err;
    do {
        err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
    } while (err<0 && errno == EINTR);

    if (err == 0 && mCtx != NULL) {
        mCtx->handleVsyncEvent(next_vsync);
    }
}

static const int64_t kOneSecondNs = 1 * 1000 * 1000 * 1000;
void KmsDisplay::VSyncThread::performVSync()
{
    uint64_t timestamp = 0;
    static uint64_t lasttime = 0;

    int drmfd = mCtx->drmfd();
    uint32_t high_crtc = (mCtx->crtcpipe() << DRM_VBLANK_HIGH_CRTC_SHIFT);
    drmVBlank vblank;
    memset(&vblank, 0, sizeof(vblank));
    vblank.request.type = (drmVBlankSeqType)(
            DRM_VBLANK_RELATIVE | (high_crtc & DRM_VBLANK_HIGH_CRTC_MASK));
    vblank.request.sequence = 1;
    int ret = drmWaitVBlank(drmfd, &vblank);
    if (ret == -EINTR) {
        ALOGE("drmWaitVBlank failed");
        return;
    }
    else if (ret) {
        ALOGI("switch to fake vsync");
        performFakeVSync();
        return;
    }
    else {
        timestamp = (uint64_t)vblank.reply.tval_sec * kOneSecondNs +
                (uint64_t)vblank.reply.tval_usec * 1000;
    }

    // bypass timestamp when it is 0.
    if (timestamp == 0) {
        return;
    }

    if (lasttime != 0) {
        ALOGV("vsync period: %" PRIu64, timestamp - lasttime);
    }

    lasttime = timestamp;
    if (mCtx != NULL) {
        mCtx->handleVsyncEvent(timestamp);
    }
}

}
