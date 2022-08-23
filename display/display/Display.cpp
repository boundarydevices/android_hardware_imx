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
#include <cutils/log.h>
#include <sync/sync.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#include "Memory.h"
#include "MemoryDesc.h"
#include <g2dExt.h>
#include "Display.h"

namespace fsl {

static inline int compare_type(const DisplayConfig& lhs, const DisplayConfig& rhs)
{
    if (lhs.mXres == rhs.mXres && lhs.mYres == rhs.mYres) {
        return 0;
    }
    else if (lhs.mXres > rhs.mXres || lhs.mYres > rhs.mYres) {
        return 1;
    }
    return -1;
}

Display::Display()
    : mLock(Mutex::PRIVATE),
      mComposer(*Composer::getInstance())
{
    mConfigs.clear();
    mActiveConfig = -1;
    mFirstConfigId = 0;
    memset(&mBackupConfig, 0, sizeof(mBackupConfig));
    mRefreshRequired = false;

    for (size_t i=0; i<MAX_LAYERS; i++) {
        mLayers[i] = new Layer();
        mLayers[i]->index = i;
        mHwLayers[i] = nullptr;
    }

    invalidLayers();
    mConnected = false;
    mType = DISPLAY_INVALID;
    mRenderTarget = NULL;
    mAcquireFence = -1;
    mIndex = -1;
    mComposeFlag = 0;
    mEdid = NULL;
    mTileHwLimit = 0;
    mResetHdrMode = false;
    mUiUpdate = false;
    mListener = NULL;
    mTotalLayerNum = 0;
    mMaxBrightness = -1;
    mOverlay = NULL;

#ifdef DEBUG_DUMP_REFRESH_RATE
    m_pre_commit_start = 0;
    m_pre_commit_time = 0;
    m_total_sf_delay = 0;
    m_total_commit_time = 0;
    m_total_commit_cost = 0;
    m_commit_cnt = 0;
    m_request_refresh_cnt = 0;
#endif
}

Display::~Display()
{
    invalidLayers();

    Mutex::Autolock _l(mLock);
    mConfigs.clear();
    for (size_t i=0; i<MAX_LAYERS; i++) {
        if (mLayers[i]) {
            delete mLayers[i];
            mLayers[i] = NULL;
        }
    }

    mRenderTarget = NULL;
    if (mAcquireFence != -1) {
        close(mAcquireFence);
        mAcquireFence = -1;
    }
}

void Display::setCallback(EventListener* callback)
{
    Mutex::Autolock _l(mLock);
    mListener = callback;
}

int Display::setPowerMode(int /*mode*/)
{
    return -1;
}

void Display::enableVsync()
{
}

void Display::setVsyncEnabled(bool /*enabled*/)
{
}

void Display::setFakeVSync(bool)
{
}

void Display::setConnected(bool connected)
{
    Mutex::Autolock _l(mLock);
    mConnected = connected;
}

bool Display::connected()
{
    Mutex::Autolock _l(mLock);
    return mConnected;
}

void Display::setType(int type)
{
    Mutex::Autolock _l(mLock);
    mType = type;
}

int Display::type()
{
    Mutex::Autolock _l(mLock);
    return mType;
}

void Display::setIndex(int index)
{
    Mutex::Autolock _l(mLock);
    mIndex = index;
}

int Display::index()
{
    Mutex::Autolock _l(mLock);
    return mIndex;
}

void Display::clearConfigs()
{
    Mutex::Autolock _l(mLock);

    mConfigs.clear();
    mActiveConfig = -1;
}

int Display::findDisplayConfig(int width, int height, float fps, int format)
{
    int i;
    for (i=0; i<mConfigs.size(); i++) {
        const DisplayConfig& cfg = mConfigs[mFirstConfigId + i];
        // if not specify the format(0), only compare the resolution
        // if the foramt is not 0, both format and resolution need to compare
        if ((((format != -1) && (cfg.mFormat == format)) || (format == -1))
            && (cfg.mXres == width) && (cfg.mYres == height) && (fabs(cfg.mFps - fps) < FLOAT_TOLERANCE))
            break;
    }

    return i >= mConfigs.size() ? -1 : mFirstConfigId + i;
}

int Display::createDisplayConfig(int width, int height, float fps, int format)
{
    Mutex::Autolock _l(mLock);

    int id = findDisplayConfig(width, height, fps, format);
    if (id < 0) {
        DisplayConfig config;
        config.mXres = width;
        config.mYres = height;
        if (format != -1) {
            config.mFormat = format;
        }
        if (fabs(fps) >= FLOAT_TOLERANCE)
            config.mFps = fps;
        else
            config.mFps = DEFAULT_REFRESH_RATE;

        mConfigs.emplace(mFirstConfigId, config);
        id = mFirstConfigId;
    }
    mActiveConfig = id;

    return id;
}

int Display::getConfigNum()
{
    Mutex::Autolock _l(mLock);
    return mConfigs.size();
}

int Display::getFirstConfigId()
{
    return mFirstConfigId;
}

bool Display::isOverlayEnabled()
{
    char value[PROPERTY_VALUE_MAX];
    property_get("vendor.hwc.enable.overlay", value, "0");
    int useOverlay = atoi(value);
    if (useOverlay == 1) {
        return true;
    }
    return false;
}

bool Display::isHdrSupported()
{
    Mutex::Autolock _l(mLock);
    if(mEdid == NULL)
        return false;
    return mEdid->isHdrSupported();
}

int Display::getHdrMetaData(HdrMetaData* hdrMetaData)
{
    Mutex::Autolock _l(mLock);
    if(mEdid == NULL)
        return -EINVAL;
    return mEdid->getHdrMetaData(hdrMetaData);
}

int Display::getHdrSupportTypes(uint32_t* numTypes, int32_t* hdrTypes)
{
    Mutex::Autolock _l(mLock);
    if(mEdid == NULL)
        return -EINVAL;
    return mEdid->getHdrSupportTypes(numTypes, hdrTypes);
}

const DisplayConfig& Display::getConfig(int config)
{
    Mutex::Autolock _l(mLock);
    return mConfigs[config];
}

const DisplayConfig& Display::getActiveConfig()
{
    Mutex::Autolock _l(mLock);
    return mConfigs[mActiveConfig];
}

int Display::setActiveConfig(int config)
{
    Mutex::Autolock _l(mLock);
    mActiveConfig = config;
    return 0;
}

int Display::getActiveId()
{
    Mutex::Autolock _l(mLock);
    return mActiveConfig;
}

int Display::getFormatSize(int format)
{
    int bpp;
    switch (format) {
        case FORMAT_RGBA8888:
        case FORMAT_RGBX8888:
        case FORMAT_BGRA8888:
        case FORMAT_RGBA1010102:
            bpp = 4;
            break;
        case FORMAT_RGB888:
            bpp = 3;
            break;
        case FORMAT_RGB565:
            bpp = 2;
            break;
        case FORMAT_RGBAFP16:
            bpp = 8;
            break;
        default:
            bpp = 4;
            break;
    }

    return bpp;
}

bool Display::checkOverlay(Layer* /*layer*/)
{
    return false;
}

int Display::performOverlay()
{
    return 0;
}

int Display::updateScreen()
{
    return -EINVAL;
}

int Display::setRenderTarget(Memory* buffer, int acquireFence)
{
    Mutex::Autolock _l(mLock);
    if (mAcquireFence != -1) {
        close(mAcquireFence);
    }

    mRenderTarget = buffer;
    mAcquireFence = acquireFence;

    return 0;
}

void Display::resetLayerLocked(Layer* layer)
{
    if (layer == NULL) {
        return;
    }

    layer->busy = false;
    layer->zorder = 0;
    layer->origType = LAYER_TYPE_INVALID;
    layer->type = LAYER_TYPE_INVALID;
    layer->handle = NULL;
    layer->transform = 0;
    layer->blendMode = 0;
    layer->color = 0;
    layer->flags = 0;
    layer->sourceCrop.clear();
    layer->displayFrame.clear();
    layer->visibleRegion.clear();
    if (layer->acquireFence != -1) {
        close(layer->acquireFence);
    }
    layer->acquireFence = -1;
    layer->releaseFence = -1;
    layer->isHdrMode = false;
    layer->priv = NULL;
    layer->lastHandle = NULL;
    layer->isOverlay = false;
    layer->lastSourceCrop.clear();
    layer->lastDisplayFrame.clear();
    memset(&layer->hdrMetadata, 0, sizeof(layer->hdrMetadata));
}

void Display::releaseLayer(int index)
{
    if (index < 0 || index >= MAX_LAYERS) {
        ALOGE("%s invalid index:%d", __func__, index);
        return;
    }

    Mutex::Autolock _l(mLock);
    if (mLayers[index] == NULL) {
        return;
    }

    if (mLayers[index]->isHdrMode) {
        mResetHdrMode = true;
    }
    resetLayerLocked(mLayers[index]);
    mLayerVector.remove(mLayers[index]);
}

Layer* Display::getLayer(int index)
{
    if (index < 0 || index >= MAX_LAYERS) {
        ALOGE("%s invalid index:%d", __func__, index);
        return NULL;
    }

    Mutex::Autolock _l(mLock);
    if (mLayers[index]->busy) {
        return mLayers[index];
    }
    else {
        return NULL;
    }
}

Layer* Display::getLayerByPriv(void* priv)
{
    Mutex::Autolock _l(mLock);
    Layer* layer = NULL;
    for (size_t i=0; i<MAX_LAYERS; i++) {
        if (!mLayers[i]->busy) {
            continue;
        }

        if (mLayers[i]->priv == priv) {
            layer = mLayers[i];
            break;
        }
    }

    return layer;
}

Layer* Display::getFreeLayer()
{
    Mutex::Autolock _l(mLock);
    Layer* layer = NULL;
    for (size_t i=0; i<MAX_LAYERS; i++) {
        if (!mLayers[i]->busy) {
            mLayers[i]->busy = true;
            layer = mLayers[i];
            break;
        }
    }

    return layer;
}

int Display::getRequests(int32_t* outDisplayRequests, uint32_t* outNumRequests,
                         uint64_t* outLayers, int32_t* outLayerRequests)
{
    if (outDisplayRequests != NULL) {
        *outDisplayRequests = 0;
    }

    if (outNumRequests != NULL) {
        *outNumRequests = 0;
    }

    if (outLayers != NULL && outLayerRequests != NULL) {
        *outLayers = 0;
        *outLayerRequests = 0;
    }

    return 0;
}

int Display::getReleaseFences(uint32_t* outNumElements, uint64_t* outLayers,
                              int32_t* outFences)
{
    uint32_t numElements = 0;

    Mutex::Autolock _l(mLock);
    for (size_t i=0; i<MAX_LAYERS; i++) {
        if (!mLayers[i]->busy) {
            continue;
        }

        if (mLayers[i]->releaseFence != -1) {
            if (outLayers != NULL && outFences != NULL) {
                if (mLayers[i]->releaseFence > 0) { // check if such release fence valid or not
                    int fd = mLayers[i]->releaseFence;
                    struct stat _stat;
                    int ret = -1;
                    if (!fcntl(fd, F_GETFL))
                        if (!fstat(fd, &_stat))
                            if (_stat.st_nlink >= 1)
                                ret = 0;

                    if (ret == -1) { // mark invalid release fence as -1
                        mLayers[i]->releaseFence = -1;
                        continue;
                    }
                }
                outLayers[numElements] = (uint64_t)mLayers[i]->index;
                outFences[numElements] = (int32_t)mLayers[i]->releaseFence;
            }
            numElements++;
        }
    }

    if (outNumElements) {
        *outNumElements = numElements;
    }

    return 0;
}

int Display::getChangedTypes(uint32_t* outNumTypes, uint64_t* outLayers,
                             int32_t* outTypes)
{
    uint32_t numTypes = 0;

    Mutex::Autolock _l(mLock);
    for (size_t i=0; i<MAX_LAYERS; i++) {
        if (!mLayers[i]->busy) {
            continue;
        }

        if (mLayers[i]->type != mLayers[i]->origType) {
            if (outLayers != NULL && outTypes != NULL) {
                outLayers[numTypes] = (uint64_t)mLayers[i]->index;
                outTypes[numTypes] = (int32_t)mLayers[i]->type;
            }
            numTypes++;
        }
    }

    if (outNumTypes) {
        *outNumTypes = numTypes;
    }

    return 0;
}

bool Display::check2DComposition()
{
    bool use2DComposition = false;
    bool rotationCap = mComposer.isFeatureSupported(G2D_ROTATION);

    // set device compose to false if composer is invalid or disabled.
    if (!mComposer.isValid() || mComposer.isDisabled()) {
        return false;
    }

    // evs camera case force device composition.
    if (forceVync()) {
        return true;
    }

    // set device compose if force set to 2DComposition.
    if (mComposer.is2DComposition()) {
        use2DComposition = true;
    }

    for (size_t i=0; i<MAX_LAYERS; i++) {
        // hw layer must be handled by device.
        if (mHwLayers[i] != nullptr) {
            return true;
        }

        if (!mLayers[i]->busy) {
            continue;
        }

        // vpu tile format must be handled by device.
        Memory* memory = mLayers[i]->handle;
        if (memory != nullptr && memory->fslFormat == FORMAT_NV12_TILED) {
            return true;
        }

        // rotation case skip device composition.
        if (mLayers[i]->transform != 0 && !rotationCap) {
            use2DComposition = false;
            ALOGV("g2d can't support rotation");
        }

#ifdef WORKAROUND_DPU_ALPHA_BLENDING
        // pixel alpha + blending + global alpha case skip device composition.
        if (memory != nullptr && mLayers[i]->planeAlpha != 0xff && mLayers[i]->blendMode == BLENDING_PREMULT &&
            (memory->fslFormat == FORMAT_RGBA8888 || memory->fslFormat == FORMAT_BGRA8888 ||
             memory->fslFormat == FORMAT_RGBA1010102 || memory->fslFormat == FORMAT_RGBAFP16))
        {
            use2DComposition = false;
            ALOGV("%s,%d,%x,%x,%x",__func__,__LINE__,memory->fslFormat,mLayers[i]->planeAlpha,mLayers[i]->blendMode);
        }
#endif

    }

    return use2DComposition;
}

bool Display::triggerComposition()
{
    Mutex::Autolock _l(mLock);
    return directCompositionLocked();

}
bool Display::directCompositionLocked()
{
    bool force = false;

    // surfaceflinger layers triggered by surfaceflinger.
    for (size_t i=0; i<MAX_LAYERS; i++) {
        if (mLayers[i]->busy) {
            return force;
        }
    }

    // hw layers.
    for (size_t i=0; i<MAX_LAYERS; i++) {
        if (mHwLayers[i] != nullptr) {
            force = true;
            break;
        }
    }

    return force;
}

bool Display::verifyLayers()
{
    bool deviceCompose = true;
    mUiUpdate = false;
    static int only_overlay_cnt = 0;

    Mutex::Autolock _l(mLock);
    mLayerVector.clear();

    int lastComposeFlag = mComposeFlag;
    int lastTotalLayerNum = mTotalLayerNum;
    mTotalLayerNum = 0;

    // get deviceCompose init value
    deviceCompose = check2DComposition();

#ifdef HAVE_UNMAPPED_HEAP
    bool hasSecureLayer = false;
#endif
    std::vector<int32_t> zorderVector;
    for (size_t i=0; i<MAX_LAYERS; i++) {
        if (!mLayers[i]->busy) {
            continue;
        }

        if (mLayers[i]->flags & SKIP_LAYER) {
            deviceCompose = false;
            mLayers[i]->type = LAYER_TYPE_CLIENT;
        }

        switch (mLayers[i]->origType) {
            case LAYER_TYPE_CLIENT:
                ALOGV("client type detected");
                deviceCompose = false;
                break;

            case LAYER_TYPE_SOLID_COLOR:
                ALOGV("solid color type detected");
                mLayers[i]->blendMode = BLENDING_DIM;
                mLayers[i]->type = LAYER_TYPE_SOLID_COLOR;
                break;

            case LAYER_TYPE_SIDEBAND:
                //set side band parameters.
                mLayers[i]->type = LAYER_TYPE_SIDEBAND;
                break;

            case LAYER_TYPE_DEVICE:
                mLayers[i]->type = LAYER_TYPE_DEVICE;
                break;

            case LAYER_TYPE_CURSOR:
                mLayers[i]->type = LAYER_TYPE_DEVICE;
                break;

            default:
                ALOGE("verifyLayers: invalid type:%d", mLayers[i]->origType);
                deviceCompose = false;
                break;
        }
    }

    // set device compose to false if composer is invalid or disabled.
    if (!mComposer.isValid() || mComposer.isDisabled()) {
        deviceCompose = false;
    }

    // set device compose flags.
    int ovIdx = -1, idx;
    mComposeFlag &= OVERLAY_COMPOSE_MASK;
    mComposeFlag = mComposeFlag << (LAST_OVERLAY_BIT - OVERLAY_COMPOSE_BIT);
    for (size_t i=0; i<MAX_LAYERS; i++) {
        if (!mLayers[i]->busy) {
            continue;
        }

        // handle overlay.
        idx = i;
        if (checkOverlay(mLayers[i])) {
            if (ovIdx == -1) {
                ovIdx = i;
                continue;
            }
#ifdef HAVE_UNMAPPED_HEAP
            else {
                Memory *ov_hnd = mLayers[ovIdx]->handle;
                Memory *hnd = mLayers[i]->handle;
                if (hnd != NULL && (hnd->usage & USAGE_PROTECTED)
                    && ov_hnd != NULL && !(ov_hnd->usage & USAGE_PROTECTED)) {
                    mLayers[ovIdx]->isOverlay = false;
                    idx = ovIdx; // previous overlay layer restore to CLIENT type
                    ovIdx = i;
                    hasSecureLayer = true;
                }
            }
#endif
        }

        if (!deviceCompose) {
            mLayers[idx]->type = LAYER_TYPE_CLIENT;
            mComposeFlag |= 1 << CLIENT_COMPOSE_BIT;
            mTotalLayerNum++;
            zorderVector.emplace_back(mLayers[idx]->zorder);

            // Here compare current layer info with previous one to determine
            // whether UI has update. IF no update,won't commit to framebuffer
            // to avoid UI re-composition.
            if (mLayers[idx]->handle != mLayers[idx]->lastHandle ||
                    mLayers[idx]->lastSourceCrop != mLayers[idx]->sourceCrop ||
                    mLayers[idx]->lastDisplayFrame != mLayers[idx]->displayFrame){
                mUiUpdate = true;
            }

            mLayers[idx]->lastHandle = mLayers[idx]->handle;
            mLayers[idx]->lastSourceCrop = mLayers[idx]->sourceCrop;
            mLayers[idx]->lastDisplayFrame = mLayers[idx]->displayFrame;
            continue;
        }
        mLayerVector.add(mLayers[idx]);
        mTotalLayerNum++;
    }

    if (ovIdx >= 0) {
        bool shouldOverlay = true;
        for (auto zorder : zorderVector) {
            if (zorder < mLayers[ovIdx]->zorder) {
                shouldOverlay = false;
                break;
            }
        }

#ifdef HAVE_UNMAPPED_HEAP
        if (hasSecureLayer) {
            shouldOverlay = true; //SecureLayer always use overlay
        }
#endif
        if (shouldOverlay) {
            mOverlay = mLayers[ovIdx];
            mOverlay->isOverlay = true;
            mOverlay->type = LAYER_TYPE_DEVICE;
            mComposeFlag |= 1 << OVERLAY_COMPOSE_BIT;
        } else {
            mLayers[ovIdx]->type = LAYER_TYPE_CLIENT;
            mComposeFlag |= 1 << CLIENT_COMPOSE_BIT;
            mTotalLayerNum++;
        }
    }

    if (mTotalLayerNum != lastTotalLayerNum) {
        mUiUpdate = true;
    }

    for (size_t i=0; i<MAX_LAYERS; i++) {
        if (!mLayers[i]->busy) {
            continue;
        }
        if (mLayers[i]->sourceCrop.left == 0 && mLayers[i]->sourceCrop.top == 0
            && mLayers[i]->sourceCrop.right == 0 && mLayers[i]->sourceCrop.bottom == 0
            && (mComposeFlag & OVERLAY_COMPOSE_MASK))
            mLayers[i]->type = LAYER_TYPE_DEVICE; // always skip "Background for SurfaceView[xxx] layer"
    }

    // If all layer is in overlay state,add ONLY_OVERLAY_BIT to mComposeFlag.
    // But only add it at the first time.
    lastComposeFlag &= ~ONLY_OVERLAY_MASK;
    if ( ((mComposeFlag & CLIENT_COMPOSE_MASK) ^ CLIENT_COMPOSE_MASK) &&
        mComposeFlag & OVERLAY_COMPOSE_MASK ) {
        if (lastComposeFlag & CLIENT_COMPOSE_MASK) {
            only_overlay_cnt = 1; // clear target buffer at least twice
        }

        if ( lastComposeFlag == mComposeFlag) {
            if (only_overlay_cnt) {
                only_overlay_cnt--;
                mComposeFlag |= 1 << ONLY_OVERLAY_BIT;
            } else {
                mComposeFlag &= ~ONLY_OVERLAY_MASK;
            }
        }
        else {
            mComposeFlag |= 1 << ONLY_OVERLAY_BIT;
        }
    }

    for (size_t i=0; i<MAX_LAYERS; i++) {
        if (mHwLayers[i] == nullptr) {
            continue;
        }
        if (!deviceCompose) {
            ALOGI("please enable hwc property, valid:%d, disabled:%d",
                    mComposer.isValid(), mComposer.isDisabled());
            break;
        }
    }

    if ((mComposeFlag & 1 << OVERLAY_COMPOSE_BIT) &&
            (mComposeFlag & 1 << LAST_OVERLAY_BIT) && !mUiUpdate) {
        for (size_t i=0; i<MAX_LAYERS; i++) {
            if (!mLayers[i]->busy) {
                continue;
            }
            if (mLayers[i]->isOverlay)
                continue;
            if (mLayers[i]->type == LAYER_TYPE_CLIENT) {
                mLayers[i]->type = LAYER_TYPE_DEVICE;
            }
        }
    }

    return deviceCompose;
}

int Display::invalidLayers()
{
    Mutex::Autolock _l(mLock);
    for (size_t i=0; i<MAX_LAYERS; i++) {
        resetLayerLocked(mLayers[i]);
    }
    mLayerVector.clear();

    return 0;
}

void Display::setLayerInfo(int index, Layer* layer)
{
    if (index < 0 || index >= MAX_LAYERS) {
        ALOGE("%s invalid index:%d", __func__, index);
        return;
    }

    Mutex::Autolock _l(mLock);

    mLayers[index]->busy = layer->busy;
    mLayers[index]->zorder = layer->zorder;
    mLayers[index]->origType = layer->origType;
    mLayers[index]->type = layer->type;
    mLayers[index]->handle = layer->handle;
    mLayers[index]->lastHandle = NULL;
    mLayers[index]->transform = layer->transform;
    mLayers[index]->blendMode = layer->blendMode;
    mLayers[index]->planeAlpha = layer->planeAlpha;
    mLayers[index]->color = layer->color;
    mLayers[index]->flags = layer->flags;
    mLayers[index]->sourceCrop.left = layer->sourceCrop.left;
    mLayers[index]->sourceCrop.top = layer->sourceCrop.top;
    mLayers[index]->sourceCrop.right = layer->sourceCrop.right;
    mLayers[index]->sourceCrop.bottom = layer->sourceCrop.bottom;
    mLayers[index]->displayFrame.left = layer->displayFrame.left;
    mLayers[index]->displayFrame.top = layer->displayFrame.top;
    mLayers[index]->displayFrame.right = layer->displayFrame.right;
    mLayers[index]->displayFrame.bottom = layer->displayFrame.bottom;
    mLayers[index]->visibleRegion = layer->visibleRegion;
    mLayers[index]->lastSourceCrop.clear();
    mLayers[index]->lastDisplayFrame.clear();
    mLayers[index]->acquireFence = layer->acquireFence;
    mLayers[index]->releaseFence = layer->releaseFence;
    mLayers[index]->index = layer->index;
    mLayers[index]->isHdrMode = layer->isHdrMode;
    mLayers[index]->isOverlay = layer->isOverlay;
    mLayers[index]->priv = layer->priv;
}

int Display::setSkipLayer(bool skip)
{
    Mutex::Autolock _l(mLock);
    for (size_t i=0; i<MAX_LAYERS; i++) {
        if (skip)
            mLayers[i]->flags |= SKIP_LAYER;
        else
            mLayers[i]->flags &= ~SKIP_LAYER;
    }

    return 0;
}

int Display::addHwLayer(uint32_t index, Layer *layer)
{
    if (layer == nullptr || index >= MAX_LAYERS) {
        return -EINVAL;
    }

    Mutex::Autolock _l(mLock);
    if (mHwLayers[index] != nullptr) {
        ALOGI("%s slot:%uz will be override", __func__, index);
    }
    mHwLayers[index] = layer;

    return 0;
}

int Display::removeHwLayer(uint32_t index)
{
    if (index >= MAX_LAYERS) {
        return -EINVAL;
    }

    Mutex::Autolock _l(mLock);
    if (mHwLayers[index] == nullptr) {
        return 0;
    }

    mLayerVector.remove(mHwLayers[index]);
    mHwLayers[index] = nullptr;

    return 0;
}

int Display::composeLayers()
{
    Mutex::Autolock _l(mLock);

    return composeLayersLocked();
}

void Display::waitOnFenceLocked()
{
    // release all layer fence.
    for (size_t i=0; i<MAX_LAYERS; i++) {
        if (!mLayers[i]->busy) {
            continue;
        }

        if (mLayers[i]->acquireFence != -1) {
            sync_wait(mLayers[i]->acquireFence, -1);
            close(mLayers[i]->acquireFence);
            mLayers[i]->acquireFence = -1;
        }
    }
}

bool Display::forceVync()
{
    for (size_t i = 0; i < MAX_LAYERS; i++) {
        if (mHwLayers[i] == nullptr) {
            continue;
        }
        if (!(mHwLayers[i]->flags & BUFFER_SLOT)) {
            ALOGE("%s hw layer without BUFFER_SLOT flag", __func__);
            continue;
        }

        BufferSlot *queue = (BufferSlot*)mHwLayers[i]->priv;
        if (queue == nullptr) {
            ALOGE("%s hw layer without queue", __func__);
            continue;
        }
        return true;
    }

    return false;
}

void Display::triggerRefresh()
{
    for (size_t i = 0; i < MAX_LAYERS; i++) {
        if (mHwLayers[i] == nullptr) {
            continue;
        }
        if (!(mHwLayers[i]->flags & BUFFER_SLOT)) {
            ALOGE("%s hw layer without BUFFER_SLOT flag", __func__);
            continue;
        }

        BufferSlot *queue = (BufferSlot*)mHwLayers[i]->priv;
        if (queue == nullptr) {
            ALOGE("%s hw layer without queue", __func__);
            continue;
        }

        if (queue->presentSlotCount() > 0 && mListener != nullptr) {
            mListener->onRefresh(0);
        }
    }
}

int Display::composeLayersLocked()
{
    int ret = 0;

    waitOnFenceLocked();

    if (!mConnected && mIndex != DISPLAY_PRIMARY) {
        ALOGE("composeLayersLocked display plugout");
        return -EINVAL;
    }

    performOverlay();

    if (mLayerVector.size() <= 0 && !directCompositionLocked()) {
        return ret;
    }

    // handle hw layers.
    for (size_t i=0; i<MAX_LAYERS; i++) {
        if (mHwLayers[i] == nullptr) {
            continue;
        }
        if (!(mHwLayers[i]->flags & BUFFER_SLOT)) {
            ALOGE("%s hw layer without BUFFER_SLOT flag", __func__);
            continue;
        }

        BufferSlot *queue = (BufferSlot*)mHwLayers[i]->priv;
        if (queue == nullptr) {
            ALOGE("%s hw layer without queue", __func__);
            continue;
        }

        int slot = queue->getPresentSlot();
        if (slot < 0) {
            ALOGW("%s hw layer no buffer", __func__);
            continue;
        }

        mHwLayers[i]->handle = queue->getPresentBuffer(slot);
        if (mHwLayers[i]->handle == nullptr) {
            ALOGW("%s hw layer no valid handle", __func__);
            continue;
        }
        mLayerVector.add(mHwLayers[i]);
    }

    mComposer.lockSurface(mRenderTarget);
    mComposer.setRenderTarget(mRenderTarget);
    mComposer.clearWormHole(mLayerVector);

    // to do composite.
    size_t count = mLayerVector.size();
    for (size_t i=0; i<count; i++) {
        Layer* layer = mLayerVector[i];
        if (!layer->busy){
            ALOGE("compose invalid layer");
            continue;
        }

        if (layer->type == LAYER_TYPE_SIDEBAND) {
            //set side band parameters.
            continue;
        }

        if (layer->handle != NULL && !(layer->flags & BUFFER_SLOT))
            mComposer.lockSurface(layer->handle);

        ret = mComposer.composeLayer(layer, i==0);

        if (layer->handle != NULL && !(layer->flags & BUFFER_SLOT))
            mComposer.unlockSurface(layer->handle);

        if (ret != 0) {
            ALOGE("compose layer %zu failed", i);
            break;
        }
    }

    mComposer.unlockSurface(mRenderTarget);
    mComposer.finishComposite();

    return ret;
}

void Display::initBrightness()
{
    Mutex::Autolock _l(mLock);
    if (mIndex != DISPLAY_PRIMARY) {
        mMaxBrightness = -1;
        return;
    }

    char path[PROPERTY_VALUE_MAX];
    property_get("vendor.hw.backlight.dev", path, DEF_BACKLIGHT_DEV);
    strcpy(mBrightnessPath, DEF_BACKLIGHT_PATH);
    strcat(mBrightnessPath, path);
    strcpy(path, mBrightnessPath);
    strcat(mBrightnessPath, "/brightness");
    strcat(path, "/max_brightness");

    FILE *file = fopen(path, "r");
    if (!file) {
        mMaxBrightness = -1;
        ALOGE("%s cannot open default backlight file %s", __func__,path);
        property_get("vendor.hw.backlight_backup.dev", path, DEF_BACKLIGHT_DEV);
        strcpy(mBrightnessPath, DEF_BACKLIGHT_PATH);
        strcat(mBrightnessPath, path);
        strcpy(path, mBrightnessPath);
        strcat(mBrightnessPath, "/brightness");
        strcat(path, "/max_brightness");
        file = fopen(path, "r");
    }

    if (!file) {
        mMaxBrightness = -1;
        ALOGE("%s cannot open backup backlight file %s", __func__,path);
    }
    else {
        if (fread(&mMaxBrightness,1,3,file) == 3) {
            mMaxBrightness = atoi((char *)&mMaxBrightness);
            ALOGI("%s get maxBrightness:%d",__func__,mMaxBrightness);
        }
        fclose(file);
    }

}

int Display::getMaxBrightness()
{
    Mutex::Autolock _l(mLock);
    return mMaxBrightness;
}

int Display::setBrightness(float brightness)
{
    Mutex::Autolock _l(mLock);
    if (mIndex != DISPLAY_PRIMARY || mMaxBrightness == -1) {
        return -1;
    }

    int bright = (int)(mMaxBrightness * brightness);
    FILE *file = fopen(mBrightnessPath, "w");
    if (!file) {
        ALOGE("%s can not open file %s\n", __func__,mBrightnessPath);
        return -1;
    }
    fprintf(file, "%d", bright);
    fclose(file);
    return 0;
}

void Display::setDisplayLimitation(int limit)
{
    mTileHwLimit = limit;
}

int Display::getDisplayIdentificationData(uint8_t* displayPort, uint8_t *data,
                                          uint32_t size)
{
    int len;
    uint8_t default_edid[EDID_LENGTH] = {
        // Basic info of the default edid:
        // Vendor ID: NXP, Product ID: 0, Serial Number: 0, Mfg Week: 1, Mfg Year: 2019
        // EDID Structure Version: 1.3, Monitor Name: NXP Android
        0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x3B,0x10,0x00,0x00,0x00,0x00,0x00,0x00,
        0x01,0x1D,0x01,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x64,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFC,0x00,0x4E,0x58,0x50,
        0x20,0x41,0x6E,0x64,0x72,0x6F,0x69,0x64,0x0A,0x0A,0x00,0x00,0x00,0x10,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1E,
    };

    if (mTileHwLimit > 0)
        *displayPort = (uint8_t)((mTileHwLimit << 6) | mIndex);
    else
        *displayPort = (uint8_t)mIndex;

    if (size < EDID_LENGTH)
        return -1;

    if ((mEdid == NULL) || ((len = mEdid->getEdidRawData(data, size)) <= 0)) {
        // Use NXP default EDID data if no data get from driver
        memcpy(data, default_edid, EDID_LENGTH);
        len = EDID_LENGTH;
    }

    return len;
}

int Display::getDisplayConnectionType()
{
    Mutex::Autolock _l(mLock);
    return mType;
}

nsecs_t Display::getDisplayVsyncPeroid()
{
    Mutex::Autolock _l(mLock);
    return mConfigs[mActiveConfig].mVsyncPeriod;
}

bool Display::isLowLatencyModeSupport()
{
    /* Need display to support low latency mode. If the display
    * is connected via HDMI 2.1, then Auto Low Latency Mode should be triggered. If the display is
    * internally connected, then a custom low latency mode should be triggered (if available).
    */
    Mutex::Autolock _l(mLock);
    // TODO: Need to consider the EDID inforamtion
    if (mType == DISPLAY_HDMI || mType == DISPLAY_HDMI_ON_BOARD)
        return true;
    else
        return false;
}

int Display::setAutoLowLatencyMode(bool /*on*/)
{
    /* Need display to support low latency mode.
    */
    return 0;
}

int Display::getSupportedContentTypes(int num, uint32_t *supportedTypes)
{
    // HDMI spec feature, assume it support all types
    // This list must not change after initialization.
    int i = 0;
    if (num >= DISP_CONTENT_TYPE_GAME && supportedTypes) {
        supportedTypes[i++] = DISP_CONTENT_TYPE_GRAPHICS;
        supportedTypes[i++] = DISP_CONTENT_TYPE_PHOTO;
        supportedTypes[i++] = DISP_CONTENT_TYPE_CINEMA;
        supportedTypes[i++] = DISP_CONTENT_TYPE_GAME;
    }

    return DISP_CONTENT_TYPE_GAME;  // assume support all types
}

int Display::setContentType(int /*contentType*/)
{
    /* According to the HDMI 1.4 specification, supporting all content types is optional. Whether
     * the display supports a given content type is reported by getSupportedContentTypes.
     */
    return 0;
}

int Display::getConfigGroup(int config)
{
    Mutex::Autolock _l(mLock);
    return mConfigs[config].cfgGroupId;
}

int Display::changeDisplayConfig(int config, nsecs_t /*desiredTimeNanos*/, bool /*seamlessRequired*/,
                        nsecs_t* /*outAppliedTime*/, bool* /*outRefresh*/, nsecs_t* /*outRefreshTime*/)
{
    Mutex::Autolock _l(mLock);
    setActiveConfig(config);

    return 0;
}

// ------------------BufferSlot-----------------------------
BufferSlot::BufferSlot(uint32_t count)
{
    if (count > MAX_COUNT) {
        count = MAX_COUNT;
    }

    for (uint32_t i=0; i<MAX_COUNT; i++) {
        mBuffers[i] = nullptr;
    }

    for (uint32_t i=0; i<count; i++) {
        mFreeSlot.emplace_back(i);
    }
    mPresentSlot.clear();
    mLastPresent = -1;
    mPresentTotal = 0;
}

BufferSlot::~BufferSlot()
{
    native_handle_t *handle;
    Mutex::Autolock _l(mLock);
    for (uint32_t i=0; i<MAX_COUNT; i++) {
        if (mBuffers[i] == nullptr) {
            continue;
        }
        handle = (native_handle_t*)mBuffers[i];
        native_handle_close(handle);
        native_handle_delete(handle);
    }
}

int32_t BufferSlot::getFreeSlot()
{
    Mutex::Autolock _l(mLock);
    if (mFreeSlot.empty()) {
        mCondition.wait(mLock);
    }

    int32_t slot;
    auto it = mFreeSlot.begin();
    slot = *it;
    mFreeSlot.erase(it);

    return slot;
}

int32_t BufferSlot::presentSlotCount()
{
    Mutex::Autolock _l(mLock);
    return mPresentSlot.size();
}

int32_t BufferSlot::presentTotal()
{
    Mutex::Autolock _l(mLock);
    return mPresentTotal;
}

int32_t BufferSlot::getPresentSlot()
{
    Mutex::Autolock _l(mLock);
    if (mPresentSlot.empty()) {
        return mLastPresent;
    }

    int32_t slot;
    auto it = mPresentSlot.begin();
    slot = *it;
    mPresentSlot.erase(it);

    if (mLastPresent != -1) {
        mFreeSlot.emplace_back(mLastPresent);
        mCondition.signal();
    }
    mLastPresent = slot;
    return slot;
}

Memory* BufferSlot::getPresentBuffer(int32_t slot)
{
    if (slot < 0 || slot >= (int32_t)MAX_COUNT) {
        return nullptr;
    }

    Mutex::Autolock _l(mLock);
    return mBuffers[slot];
}

void BufferSlot::addPresentSlot(int32_t slot, Memory* buffer)
{
    if (slot < 0 || slot >= (int32_t)MAX_COUNT || buffer == nullptr) {
        return;
    }

    Mutex::Autolock _l(mLock);
    mPresentSlot.emplace_back(slot);
    mPresentTotal ++;
    if (mBuffers[slot] == nullptr) {
        mBuffers[slot] = (Memory*)native_handle_clone(buffer);
    }
}

}
