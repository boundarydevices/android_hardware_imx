/*
 * Copyright 2017-2018 NXP.
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
#include <drm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "Memory.h"
#include "MemoryManager.h"
#include "KmsDisplay.h"

namespace fsl {

#define DRM_FORMAT_MOD_VENDOR_VIVANTE 0x06
#define DRM_FORMAT_MOD_VENDOR_AMPHION  0x08
#define DRM_FORMAT_MOD_VENDOR_VSI      0x09
#define DRM_FORMAT_MOD_VIVANTE_SUPER_TILED  fourcc_mod_code(VIVANTE, 2)
#define DRM_FORMAT_MOD_AMPHION_TILED fourcc_mod_code(AMPHION, 1)
#define DRM_FORMAT_MOD_VSI_G1_TILED fourcc_mod_code(VSI, 1)
#define DRM_FORMAT_MOD_VSI_G2_TILED fourcc_mod_code(VSI, 2)
#define DRM_FORMAT_MOD_VSI_G2_TILED_COMPRESSED fourcc_mod_code(VSI, 3)
#define DRM_FORMAT_P010                fourcc_code('P', '0', '1', '0')

/* HDR Metadata */
struct hdr_static_metadata {
    uint16_t eotf;
    uint16_t type;
    uint16_t display_primaries_x[3];
    uint16_t display_primaries_y[3];
    uint16_t white_point_x;
    uint16_t white_point_y;
    uint16_t max_mastering_display_luminance;
    uint16_t min_mastering_display_luminance;
    uint16_t max_fall;
    uint16_t max_cll;
    uint16_t min_cll;
};

KmsDisplay::KmsDisplay()
{
    mDrmFd = -1;
    mVsyncThread = NULL;
    mTargetIndex = 0;
    memset(&mTargets[0], 0, sizeof(mTargets));
    mMemoryManager = MemoryManager::getInstance();
    mModeset = true;
    mConnectorID = 0;
    mKmsPlaneNum = 1;
    memset(mKmsPlanes, 0, sizeof(mKmsPlanes));
    memset(&mMode, 0, sizeof(mMode));
    mCrtcID = 0;

    mPset = NULL;
    mOverlay = NULL;
    mNoResolve = false;
    mAllowModifier = false;
    mMetadataID = 0;
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
    if (mEdid != NULL) {
        delete mEdid;
    }
}

/*
 * Find the property IDs and value that match its name.
 */
void KmsDisplay::getPropertyValue(uint32_t objectID, uint32_t objectType,
                          const char *propName, uint32_t* propId,
                          uint64_t* value, int drmfd)
{
    int found = 0;
    uint64_t ivalue = 0;
    uint32_t id = 0;

    drmModeObjectPropertiesPtr pModeObjectProperties =
        drmModeObjectGetProperties(drmfd, objectID, objectType);

    if (pModeObjectProperties == NULL) {
        ALOGE("drmModeObjectGetProperties failed.");
        return;
    }

    for (uint32_t i = 0; i < pModeObjectProperties->count_props; i++) {
        drmModePropertyPtr pProperty =
            drmModeGetProperty(drmfd, pModeObjectProperties->props[i]);
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
                                  size_t tableLen, int drmfd)
{
    for (uint32_t i = 0; i < tableLen; i++) {
        getPropertyValue(objectID, objectType, table[i].name,
                 table[i].ptr, NULL, drmfd);
        if (*(table[i].ptr) == 0) {
            ALOGE("can't find property ID for \'%s\'.", table[i].name);
        }
    }
}

/*
 * Find the property IDs in group with type.
 */
void KmsPlane::getPropertyIds()
{
    struct TableProperty planeTable[] = {
        {"SRC_X",   &src_x},
        {"SRC_Y",   &src_y},
        {"SRC_W",   &src_w},
        {"SRC_H",   &src_h},
        {"CRTC_X",  &crtc_x},
        {"CRTC_Y",  &crtc_y},
        {"CRTC_W",  &crtc_w},
        {"CRTC_H",  &crtc_h},
        {"alpha",   &alpha_id},
        {"dtrc_table_ofs",   &ofs_id},
        {"FB_ID",   &fb_id},
        {"CRTC_ID", &crtc_id},
    };

    KmsDisplay::getTableProperty(mPlaneID,
                     DRM_MODE_OBJECT_PLANE,
                     planeTable, ARRAY_LEN(planeTable),
                     mDrmFd);
}

/*
 * Find the property IDs in group with type.
 */
void KmsDisplay::getKmsProperty()
{
    struct TableProperty crtcTable[] = {
        {"MODE_ID", &mCrtc.mode_id},
        {"ACTIVE",  &mCrtc.active},
    };

    struct TableProperty connectorTable[] = {
        {"CRTC_ID", &mConnector.crtc_id},
        {"DPMS", &mConnector.dpms_id},
        {"HDR_SOURCE_METADATA", &mConnector.hdr_meta_id},
    };

    getTableProperty(mCrtcID,
                     DRM_MODE_OBJECT_CRTC,
                     crtcTable, ARRAY_LEN(crtcTable),
                     mDrmFd);
    getTableProperty(mConnectorID,
                     DRM_MODE_OBJECT_CONNECTOR,
                     connectorTable, ARRAY_LEN(connectorTable),
                     mDrmFd);

    for (uint32_t i=0; i<mKmsPlaneNum; i++) {
        mKmsPlanes[i].getPropertyIds();
    }
}

/*
 * add properties to a drmModeAtomicReqPtr object.
 */
void KmsDisplay::setMetaData(drmModeAtomicReqPtr pset, MetaData *meta)
{
    if (meta == NULL) {
        return;
    }

    struct hdr_static_metadata hdr_metadata;
    hdr_metadata.eotf = 2;
    hdr_metadata.type = 0x8a48;
    hdr_metadata.display_primaries_x[0] = meta->mStaticInfo.sType1.mR.x;
    hdr_metadata.display_primaries_y[0] = meta->mStaticInfo.sType1.mR.y;
    hdr_metadata.display_primaries_x[1] = meta->mStaticInfo.sType1.mG.x;
    hdr_metadata.display_primaries_y[1] = meta->mStaticInfo.sType1.mG.y;
    hdr_metadata.display_primaries_x[2] = meta->mStaticInfo.sType1.mB.x;
    hdr_metadata.display_primaries_y[2] = meta->mStaticInfo.sType1.mB.y;
    hdr_metadata.white_point_x = meta->mStaticInfo.sType1.mW.x;
    hdr_metadata.white_point_y = meta->mStaticInfo.sType1.mW.y;
    hdr_metadata.max_mastering_display_luminance = meta->mStaticInfo.sType1.mMaxDisplayLuminance;
    hdr_metadata.min_mastering_display_luminance = meta->mStaticInfo.sType1.mMinDisplayLuminance;
    hdr_metadata.max_cll = meta->mStaticInfo.sType1.mMaxContentLightLevel;
    hdr_metadata.max_fall = meta->mStaticInfo.sType1.mMaxFrameAverageLightLevel;

    drmModeCreatePropertyBlob(mDrmFd, &hdr_metadata,
             sizeof(hdr_metadata), &mMetadataID);
    drmModeAtomicAddProperty(pset, mConnectorID,
                         mConnector.hdr_meta_id, mMetadataID);
}

void KmsDisplay::bindCrtc(drmModeAtomicReqPtr pset, uint32_t modeID)
{
    /* Specify the mode to use on the CRTC, and make the CRTC active. */
    if (mModeset) {
        drmModeAtomicAddProperty(pset, mCrtcID,
                             mCrtc.mode_id, modeID);
        drmModeAtomicAddProperty(pset, mCrtcID,
                             mCrtc.active, 1);

        /* Tell the connector to receive pixels from the CRTC. */
        drmModeAtomicAddProperty(pset, mConnectorID,
                             mConnector.crtc_id, mCrtcID);
    }
}

void KmsPlane::connectCrtc(drmModeAtomicReqPtr pset,
                    uint32_t crtc, uint32_t fb)
{
    /*
     * Specify the surface to display in the plane, and connect the
     * plane to the CRTC.
     */
    drmModeAtomicAddProperty(pset, mPlaneID,
                             fb_id, fb);
    drmModeAtomicAddProperty(pset, mPlaneID,
                             crtc_id, crtc);

}

void KmsPlane::setAlpha(drmModeAtomicReqPtr pset,
                    uint32_t alpha)
{
    /*
     * Specify the alpha of source surface to display.
     */
    drmModeAtomicAddProperty(pset, mPlaneID,
                             alpha_id, alpha);
}

void KmsPlane::setTableOffset(drmModeAtomicReqPtr pset, MetaData *meta)
{
    if (meta == NULL) {
        return;
    }

    uint32_t yoff = meta->mYOffset;
    uint32_t uvoff = meta->mUVOffset;

    /*
     * Specify the offset of table to plane.
     */
    drmModeAtomicAddProperty(pset, mPlaneID,
                             ofs_id, yoff | ((uint64_t)uvoff) << 32);
}

void KmsPlane::setSourceSurface(drmModeAtomicReqPtr pset,
                    uint32_t x, uint32_t y,
                    uint32_t w, uint32_t h)
{
    /*
     * Specify the region of source surface to display (i.e., the
     * "ViewPortIn").  Note these values are in 16.16 format, so shift
     * up by 16.
     */
    drmModeAtomicAddProperty(pset, mPlaneID,
                             src_x, x);
    drmModeAtomicAddProperty(pset, mPlaneID,
                             src_y, y);
    drmModeAtomicAddProperty(pset, mPlaneID,
                             src_w, w << 16);
    drmModeAtomicAddProperty(pset, mPlaneID,
                             src_h, h << 16);

}

void KmsPlane::setDisplayFrame(drmModeAtomicReqPtr pset,
                    uint32_t x, uint32_t y,
                    uint32_t w, uint32_t h)
{
    /*
     * Specify the region within the mode where the image should be
     * displayed (i.e., the "ViewPortOut").
     */
    drmModeAtomicAddProperty(pset, mPlaneID,
                             crtc_x, x);
    drmModeAtomicAddProperty(pset, mPlaneID,
                             crtc_y, y);
    drmModeAtomicAddProperty(pset, mPlaneID,
                             crtc_w, w);
    drmModeAtomicAddProperty(pset, mPlaneID,
                             crtc_h, h);
}

int KmsDisplay::setPowerMode(int mode)
{
    Mutex::Autolock _l(mLock);

    if (!mConnected) {
        ALOGE("%s display plugout", __func__);
        return 0;
    }

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

    // set display mode when power on.
    if (mPowerMode == DRM_MODE_DPMS_ON) {
        mModeset = true;
    }

    // Audio/Video share same clock on HDMI interface.
    // Power off HDMI will also break HDMI Audio clock.
    // So HDMI need to keep power on.
    if (mEncoderType == DRM_MODE_ENCODER_TMDS) {
        return 0;
    }

    int err = drmModeConnectorSetProperty(mDrmFd, mConnectorID,
                  mConnector.dpms_id, mPowerMode);
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

bool KmsDisplay::checkOverlay(Layer* layer)
{
    char value[PROPERTY_VALUE_MAX];
    property_get("hwc.enable.overlay", value, "1");
    int useOverlay = atoi(value);
    if (useOverlay == 0) {
        return false;
    }

    if (mKmsPlaneNum < 2) {
        ALOGV("no overlay plane found");
        return false;
    }

    if (layer == NULL || layer->handle == NULL) {
        ALOGV("updateOverlay: invalid layer or handle");
        return false;
    }

    Memory* memory = layer->handle;
    if ((memory->fslFormat >= FORMAT_RGBA8888) &&
        (memory->fslFormat <= FORMAT_BGRA8888)) {
        ALOGV("updateOverlay: invalid format");
        return false;
    }

    // overlay only needs on imx8mq and supertiled format.
    if (!((memory->usage & USAGE_PADDING_BUFFER) &&
        (memory->fslFormat == FORMAT_NV12)) &&
        !(memory->flags & FLAGS_SECURE) &&
        memory->fslFormat != FORMAT_NV12_TILED &&
        memory->fslFormat != FORMAT_NV12_G1_TILED &&
        memory->fslFormat != FORMAT_NV12_G2_TILED &&
        memory->fslFormat != FORMAT_NV12_G2_TILED_COMPRESSED &&
        memory->fslFormat != FORMAT_P010 &&
        memory->fslFormat != FORMAT_P010_TILED &&
        memory->fslFormat != FORMAT_P010_TILED_COMPRESSED) {
        return false;
    }

    // scaling limitation on imx8mq.
    if (memory->usage & USAGE_PADDING_BUFFER) {
        Rect *rect = &layer->displayFrame;
        const DisplayConfig& config = mConfigs[mActiveConfig];
        int w = (rect->right - rect->left) * mMode.hdisplay / config.mXres;
        int h = (rect->bottom - rect->top) * mMode.vdisplay / config.mYres;
        Rect *srect = &layer->sourceCrop;
        if (w > (srect->right - srect->left) * 7 ||
            h > (srect->bottom - srect->top) * 7) {
            ALOGV("work around to GPU composite");
            // fall back to GPU.
            return false;
        }
    }

    if (mOverlay != NULL) {
        ALOGW("only support one overlay now");
        return false;
    }

    mOverlay = layer;

    return true;
}

int KmsDisplay::performOverlay()
{
    Layer* layer = mOverlay;
    if (layer == NULL || layer->handle == NULL) {
        return 0;
    }

    if (!mPset) {
        mPset = drmModeAtomicAlloc();
        if (!mPset) {
            ALOGE("Failed to allocate property set");
            return -ENOMEM;
        }
    }

    Memory* buffer = layer->handle;
    const DisplayConfig& config = mConfigs[mActiveConfig];
    if (buffer->fbId == 0) {
        int format = convertFormatToDrm(buffer->fslFormat);
        int stride = buffer->stride * 1;
        uint32_t bo_handles[4] = {0};
        uint32_t pitches[4] = {0};
        uint32_t offsets[4] = {0};
        uint64_t modifiers[4] = {0};

        if (buffer->fslFormat == FORMAT_NV12_TILED) {
            modifiers[0] = DRM_FORMAT_MOD_AMPHION_TILED;
            modifiers[1] = DRM_FORMAT_MOD_AMPHION_TILED;
        }
        else if (buffer->fslFormat == FORMAT_NV12_G1_TILED ||
                buffer->fslFormat == FORMAT_P010_TILED) {
            modifiers[0] = DRM_FORMAT_MOD_VSI_G1_TILED;
            modifiers[1] = DRM_FORMAT_MOD_VSI_G1_TILED;
        }
        else if (buffer->fslFormat == FORMAT_NV12_G2_TILED) {
            modifiers[0] = DRM_FORMAT_MOD_VSI_G2_TILED;
            modifiers[1] = DRM_FORMAT_MOD_VSI_G2_TILED;
        }
        else if (buffer->fslFormat == FORMAT_NV12_G2_TILED_COMPRESSED ||
                buffer->fslFormat == FORMAT_P010_TILED_COMPRESSED) {
            modifiers[0] = DRM_FORMAT_MOD_VSI_G2_TILED_COMPRESSED;
            modifiers[1] = DRM_FORMAT_MOD_VSI_G2_TILED_COMPRESSED;
        }
        pitches[0] = stride;
        pitches[1] = stride;
        offsets[0] = 0;
        offsets[1] = stride * buffer->height;
        drmPrimeFDToHandle(mDrmFd, buffer->fd, (uint32_t*)&buffer->fbHandle);
        bo_handles[0] = buffer->fbHandle;
        bo_handles[1] = buffer->fbHandle;
        if (buffer->fslFormat == FORMAT_NV12_TILED ||
            buffer->fslFormat == FORMAT_NV12_G1_TILED ||
            buffer->fslFormat == FORMAT_NV12_G2_TILED ||
            buffer->fslFormat == FORMAT_NV12_G2_TILED_COMPRESSED ||
            buffer->fslFormat == FORMAT_P010_TILED ||
            buffer->fslFormat == FORMAT_P010_TILED_COMPRESSED) {
            drmModeAddFB2WithModifiers(mDrmFd, buffer->width, buffer->height,
                format, bo_handles, pitches, offsets, modifiers,
                (uint32_t*)&buffer->fbId, DRM_MODE_FB_MODIFIERS);
        }
        else {
            drmModeAddFB2(mDrmFd, buffer->width, buffer->height, format,
                    bo_handles, pitches, offsets, (uint32_t*)&buffer->fbId, 0);
        }
        buffer->kmsFd = mDrmFd;
    }

    if (buffer->fbId == 0) {
        ALOGE("%s invalid fbid", __func__);
        mOverlay = NULL;
        return 0;
    }

    MetaData * meta = MemoryManager::getInstance()->getMetaData(buffer);
    if (meta != NULL && meta->mFlags & FLAGS_META_CHANGED) {
        setMetaData(mPset, meta);
        meta->mFlags &= ~FLAGS_META_CHANGED;
        meta++;
    }
    if (meta != NULL && meta->mFlags & FLAGS_COMPRESSED_OFFSET) {
        mKmsPlanes[mKmsPlaneNum - 1].setTableOffset(mPset, meta);
        meta->mFlags &= ~FLAGS_COMPRESSED_OFFSET;
    }
    mKmsPlanes[mKmsPlaneNum - 1].connectCrtc(mPset, mCrtcID, buffer->fbId);

    Rect *rect = &layer->displayFrame;
    int x = rect->left * mMode.hdisplay / config.mXres;
    int y = rect->top * mMode.vdisplay / config.mYres;
    int w = (rect->right - rect->left) * mMode.hdisplay / config.mXres;
    int h = (rect->bottom - rect->top) * mMode.vdisplay / config.mYres;
#ifdef WORKAROUND_DOWNSCALE_LIMITATION
    mKmsPlanes[mKmsPlaneNum - 1].setDisplayFrame(mPset, x, y, ALIGN_PIXEL_2(w), ALIGN_PIXEL_2(h));
#else
    mKmsPlanes[mKmsPlaneNum - 1].setDisplayFrame(mPset, x, y, w, h);
#endif

    rect = &layer->sourceCrop;
#ifdef WORKAROUND_DOWNSCALE_LIMITATION
    mKmsPlanes[mKmsPlaneNum - 1].setSourceSurface(mPset, 0, 0,
                    ALIGN_PIXEL_2(w), ALIGN_PIXEL_2(h));
#else
    mKmsPlanes[mKmsPlaneNum - 1].setSourceSurface(mPset, rect->left, rect->top,
                    rect->right - rect->left, rect->bottom - rect->top);
#endif

    mOverlay = NULL;

    return true;
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
        int format = convertFormatToDrm(config.mFormat);
        int stride = buffer->stride * config.mBytespixel;
        uint32_t bo_handles[4] = {0};
        uint32_t pitches[4] = {0};
        uint32_t offsets[4] = {0};
        uint64_t modifiers[4] = {0};

        mNoResolve = mAllowModifier && (buffer->usage &
                (USAGE_GPU_TILED_VIV | USAGE_GPU_TS_VIV));
        if (mNoResolve) {
            modifiers[0] = DRM_FORMAT_MOD_VIVANTE_SUPER_TILED;
        }

        pitches[0] = stride;
        drmPrimeFDToHandle(mDrmFd, buffer->fd, (uint32_t*)&buffer->fbHandle);
        bo_handles[0] = buffer->fbHandle;
        if (mNoResolve) {
            /* workaround GPU SUPER_TILED R/B swap issue, for no-resolve and tiled output
              GPU not distinguish A8B8G8R8 and A8R8G8B8, all regard as A8R8G8B8, need do
              R/B swap here for no-resolve and tiled buffer */
            if (format == DRM_FORMAT_XBGR8888) format = DRM_FORMAT_XRGB8888;
            if (format == DRM_FORMAT_ABGR8888) format = DRM_FORMAT_ARGB8888;

            drmModeAddFB2WithModifiers(mDrmFd, ALIGN_PIXEL_64(buffer->width), ALIGN_PIXEL_64(buffer->height),
                format, bo_handles, pitches, offsets, modifiers,
                (uint32_t*)&buffer->fbId, DRM_MODE_FB_MODIFIERS);
        }
        else {
            drmModeAddFB2(mDrmFd, buffer->width, buffer->height, format,
                    bo_handles, pitches, offsets, (uint32_t*)&buffer->fbId, 0);
        }
        buffer->kmsFd = mDrmFd;
    }

    if (buffer->fbId == 0) {
        ALOGE("%s invalid fbid", __func__);
        return 0;
    }

    if (!mPset) {
        mPset = drmModeAtomicAlloc();
        if (!mPset) {
            ALOGE("Failed to allocate property set");
            return -ENOMEM;
        }
    }

    uint32_t modeID = 0;
    uint32_t flags = DRM_MODE_ATOMIC_NONBLOCK;
    if (mModeset) {
        flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
        drmModeCreatePropertyBlob(drmfd, &mMode, sizeof(mMode), &modeID);
    }

    // to clear screen to black in below case:
    // it is not in client composition and
    // last overlay state and current are different.
    if (buffer->base != 0 && ((mComposeFlag & CLIENT_COMPOSE_MASK) ^
             CLIENT_COMPOSE_MASK) && ((mComposeFlag >> 1) ^
             (mComposeFlag & OVERLAY_COMPOSE_MASK))) {
        memset((void*)buffer->base, 0, buffer->size);
    }

    bindCrtc(mPset, modeID);
    mKmsPlanes[0].connectCrtc(mPset, mCrtcID, buffer->fbId);
    mKmsPlanes[0].setSourceSurface(mPset, 0, 0, config.mXres, config.mYres);
    mKmsPlanes[0].setDisplayFrame(mPset, 0, 0, mMode.hdisplay, mMode.vdisplay);

    for (uint32_t i=0; i<3; i++) {
        int ret = drmModeAtomicCommit(drmfd, mPset, flags, NULL);
        if (ret == -EBUSY) {
            ALOGV("commit pset busy and try again");
            usleep(1000);
            continue;
        }
        else if (ret != 0) {
            ALOGI("Failed to commit pset ret=%d", ret);
        }
        break;
    }

    drmModeAtomicFree(mPset);
    mPset = NULL;
    if (mModeset) {
        mModeset = false;
        drmModeDestroyPropertyBlob(drmfd, modeID);
    }
    if (mMetadataID != 0) {
        drmModeDestroyPropertyBlob(drmfd, mMetadataID);
    }

    return 0;
}

void KmsDisplay::getGUIResolution(int &width, int &height)
{
    // keep resolution if less than 1080p.
    if (width <= 1920) {
        return;
    }

    char value[PROPERTY_VALUE_MAX];
    memset(value, 0, sizeof(value));
    property_get("ro.boot.gui_resolution", value, "p");
    if (!strncmp(value, "4k", 2)) {
        width = 3840;
        height = 2160;
    }
    else if (!strncmp(value, "1080p", 5)) {
        width = 1920;
        height = 1080;
    }
    else if (!strncmp(value, "720p", 4)) {
        width = 1280;
        height = 720;
    }
    else if (!strncmp(value, "480p", 4)) {
        width = 640;
        height = 480;
    }
}

int KmsDisplay::openKms()
{
    Mutex::Autolock _l(mLock);

    if (mDrmFd < 0 || mConnectorID == 0) {
        ALOGE("%s invalid drmfd or connector id", __func__);
        return -ENODEV;
    }

    drmModeResPtr pModeRes = drmModeGetResources(mDrmFd);
    if (!pModeRes) {
        ALOGE("Failed to get DrmResources resources");
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

    uint64_t modifier = 0;
    ret = drmGetCap(mDrmFd, DRM_CAP_ADDFB2_MODIFIERS, &modifier);
    mAllowModifier = (modifier == 0) ? false : true;

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

    getDisplayMode(pConnector);
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

    mEncoderType = pEncoder->encoder_type;

    drmModeFreeEncoder(pEncoder);
    getPrimaryPlane();
    getKmsProperty();

    int width = mMode.hdisplay;
    int height = mMode.vdisplay;
    getGUIResolution(width, height);

    ssize_t configId = getConfigIdLocked(width, height);
    if (configId < 0) {
        ALOGE("can't find config: w:%d, h:%d", width, height);
        return -1;
    }

    int format = FORMAT_RGBX8888;
    drmModePlanePtr planePtr = drmModeGetPlane(mDrmFd, mKmsPlanes[0].mPlaneID);
    for (uint32_t i = 0; i < planePtr->count_formats; i++) {
        ALOGV("enum format:%c%c%c%c", planePtr->formats[i]&0xFF,
                (planePtr->formats[i]>>8)&0xFF,
                (planePtr->formats[i]>>16)&0xFF,
                (planePtr->formats[i]>>24)&0xFF);
        if (planePtr->formats[i] == DRM_FORMAT_ABGR8888) {
            format = FORMAT_RGBA8888;
            break;
        }
    }
    drmModeFreePlane(planePtr);

    DisplayConfig& config = mConfigs.editItemAt(configId);
    // the mmWidth and mmHeight is 0 when is not connected.
    // set the default dpi to 160.
    if (pConnector->mmWidth != 0) {
        config.mXdpi = mMode.hdisplay * 25400 / pConnector->mmWidth;
    }
    else {
        config.mXdpi = 160000;
    }
    if (pConnector->mmHeight != 0) {
        config.mYdpi = mMode.vdisplay * 25400 / pConnector->mmHeight;
    }
    else {
        config.mYdpi = 160000;
    }

    config.mFps  = mMode.vrefresh;
    config.mVsyncPeriod  = 1000000000 / mMode.vrefresh;
    config.mFormat = format;
    config.mBytespixel = 4;
    ALOGW("xres         = %d px\n"
          "yres         = %d px\n"
          "format       = %d\n"
          "xdpi         = %.2f ppi\n"
          "ydpi         = %.2f ppi\n"
          "fps          = %.2f Hz\n"
          "mode.width   = %d px\n"
          "mode.height  = %d px\n",
          config.mXres, config.mYres, format, config.mXdpi / 1000.0f,
          config.mYdpi / 1000.0f, config.mFps, mMode.hdisplay, mMode.vdisplay);

    if (pConnector != NULL) {
        drmModeFreeConnector(pConnector);
    }
    drmModeFreeResources(pModeRes);

    mActiveConfig = configId;
    prepareTargetsLocked();

    return 0;
}

int KmsDisplay::getDisplayMode(drmModeConnectorPtr pConnector)
{
    int index = 0;
    unsigned int delta = -1, rdelta = -1;
    char value[PROPERTY_VALUE_MAX];
    int width = 0, height = 0;
    int prefermode = 0;

    // display mode set by bootargs.
    mMode.vrefresh = 60;
    memset(value, 0, sizeof(value));
    property_get("ro.boot.displaymode", value, "p");
    if (!strncmp(value, "4k", 2)) {
        width = 4096;
        mMode.hdisplay = 3840;
        mMode.vdisplay = 2160;
    }
    else if (!strncmp(value, "1080p", 5)) {
        height = 1080;
        mMode.hdisplay = 1920;
        mMode.vdisplay = 1080;
    }
    else if (!strncmp(value, "720p", 4)) {
        height = 720;
        mMode.hdisplay = 1280;
        mMode.vdisplay = 720;
    }
    else if (!strncmp(value, "480p", 4)) {
        height = 480;
        mMode.hdisplay = 640;
        mMode.vdisplay = 480;
    }
    else {
        prefermode = 1;
        mMode.hdisplay = 1920;
        mMode.vdisplay = 1080;
        height = atoi(value);
        if (height == 0) {
            height = 1080;
        }
    }
    ALOGV("%s mode:%s, width:%d, height:%d", __func__, value, width, height);

    // find the best display mode.
    for (int i=0; i<pConnector->count_modes; i++) {
        drmModeModeInfo mode = pConnector->modes[i];
        if ((mode.type & DRM_MODE_TYPE_PREFERRED) && (prefermode == 1)) {
            index = i;
            break;
        }

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

    // display mode found in connector.
    if (pConnector->count_modes > 0) {
        mMode = pConnector->modes[index];
    }
    ALOGV("find best mode w:%d, h:%d", mMode.hdisplay, mMode.vdisplay);

    return 0;
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
                        "type", NULL, &type, mDrmFd);

        if (type == DRM_PLANE_TYPE_PRIMARY) {
            mKmsPlanes[0].mPlaneID = pPlaneRes->planes[i];
            mKmsPlanes[0].mDrmFd = mDrmFd;
        }
        if (type == DRM_PLANE_TYPE_OVERLAY && mKmsPlaneNum < KMS_PLANE_NUM) {
            mKmsPlanes[mKmsPlaneNum].mPlaneID = pPlaneRes->planes[i];
            mKmsPlanes[mKmsPlaneNum].mDrmFd = mDrmFd;
            mKmsPlaneNum++;
        }
    }

    drmModeFreePlaneResources(pPlaneRes);

    if (mKmsPlanes[0].mPlaneID == 0) {
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
    mKmsPlaneNum = 1;
    memset(mKmsPlanes, 0, sizeof(mKmsPlanes));

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
            return DRM_FORMAT_ABGR8888;
        case FORMAT_RGB565:
            return DRM_FORMAT_BGR565;
        case FORMAT_NV12:
            return DRM_FORMAT_NV12;
        case FORMAT_NV21:
            return DRM_FORMAT_NV21;
        case FORMAT_P010:
        case FORMAT_P010_TILED:
        case FORMAT_P010_TILED_COMPRESSED:
            return DRM_FORMAT_P010;
        case FORMAT_I420:
            return DRM_FORMAT_YUV420;
        case FORMAT_YV12:
            return DRM_FORMAT_YVU420;
        case FORMAT_NV16:
            return DRM_FORMAT_NV16;
        case FORMAT_YUYV:
            return DRM_FORMAT_YUYV;
        case FORMAT_NV12_TILED:
        case FORMAT_NV12_G1_TILED:
        case FORMAT_NV12_G2_TILED:
        case FORMAT_NV12_G2_TILED_COMPRESSED:
            return DRM_FORMAT_NV12;
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
        mMemoryManager->allocMemory(desc, &mTargets[i]);
    }
    mTargetIndex = 0;
}

void KmsDisplay::releaseTargetsLocked()
{
    for (int i=0; i<MAX_FRAMEBUFFERS; i++) {
        if (mTargets[i] == NULL) {
            continue;
        }
        mMemoryManager->releaseMemory(mTargets[i]);
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

    mEdid = new Edid(mDrmFd,mConnectorID);
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
    Mutex::Autolock _l(mLock);
    if (mDrmFd < 0 || mConnectorID == 0) {
        ALOGE("%s id:%d, invalid drmfd or connector id", __func__, mIndex);
        return -ENODEV;
    }

    int connected = mConnected;
    drmModeConnectorPtr pConnector = drmModeGetConnector(mDrmFd, mConnectorID);
    if (pConnector == NULL) {
        ALOGE("%s drmModeGetConnector failed for "
              "connector index %d", __func__, mConnectorID);
        return -ENODEV;
    }

    ALOGI("%s: id:%d, connection:%d, count_modes:%d, count_encoders:%d",
           __func__, mIndex, pConnector->connection,
           pConnector->count_modes, pConnector->count_encoders);
    if ((pConnector->connection == DRM_MODE_CONNECTED) &&
        (pConnector->count_modes > 0) &&
        (pConnector->count_encoders > 0)) {
        mConnected = true;
        getDisplayMode(pConnector);
    }
    else {
        mConnected = false;
    }

    if (pConnector != NULL) {
        drmModeFreeConnector(pConnector);
    }

    if (connected ^ mConnected) {
        mModeset = true;
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

    if (mFakeVSync || mCtx->mModeset) {
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
