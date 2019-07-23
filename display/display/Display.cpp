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
    mResetHdrMode = false;
    mUiUpdate = false;
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

int Display::setConfig(int width, int height, int* format)
{
    Mutex::Autolock _l(mLock);

    DisplayConfig config;
    config.mXres = width;
    config.mYres = height;
    if (format) {
        config.mFormat = *format;
    }

    mActiveConfig = mConfigs.indexOf(config);
    if (mActiveConfig < 0) {
        mActiveConfig = mConfigs.add(config);
    }

    return mActiveConfig;
}

int Display::getConfigNum()
{
    Mutex::Autolock _l(mLock);
    return mConfigs.size();
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
    return mLayers[index];
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
    // set device compose to false if composer is invalid or disabled.
    if (!mComposer.isValid() || mComposer.isDisabled()) {
        return false;
    }

    // set device compose if force set to 2DComposition.
    if (mComposer.is2DComposition()) {
        return true;
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
    }

    return false;
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
    bool rotationCap = mComposer.isFeatureSupported(G2D_ROTATION);
    mUiUpdate = false;

    Mutex::Autolock _l(mLock);
    mLayerVector.clear();

    // get deviceCompose init value
    deviceCompose = check2DComposition();
    int lastComposeFlag = mComposeFlag;

    for (size_t i=0; i<MAX_LAYERS; i++) {
        if (!mLayers[i]->busy) {
            continue;
        }

        if (mLayers[i]->transform != 0 && !rotationCap) {
            deviceCompose = false;
            ALOGV("g2d can't support rotation");
            break;
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
                break;
        }
    }

    // set device compose to false if composer is invalid or disabled.
    if (!mComposer.isValid() || mComposer.isDisabled()) {
        deviceCompose = false;
    }

    // set device compose flags.
    mComposeFlag &= OVERLAY_COMPOSE_MASK;
    mComposeFlag = mComposeFlag << (LAST_OVERLAY_BIT - OVERLAY_COMPOSE_BIT);
    for (size_t i=0; i<MAX_LAYERS; i++) {
        if (!mLayers[i]->busy) {
            continue;
        }

        // handle overlay.
        if (checkOverlay(mLayers[i])) {
            mLayers[i]->type = LAYER_TYPE_DEVICE;
            mComposeFlag |= 1 << OVERLAY_COMPOSE_BIT;
            continue;
        }

        if (!deviceCompose) {
            mLayers[i]->type = LAYER_TYPE_CLIENT;
            mComposeFlag |= 1 << CLIENT_COMPOSE_BIT;

            // Here compare current layer info with previous one to determine
            // whether UI has update. IF no update,won't commit to framebuffer
            // to avoid UI re-composition.
            if (mLayers[i]->handle != mLayers[i]->lastHandle ||
                    mLayers[i]->lastSourceCrop != mLayers[i]->sourceCrop ||
                    mLayers[i]->lastDisplayFrame != mLayers[i]->displayFrame){
                mUiUpdate = true;
            }

            mLayers[i]->lastHandle = mLayers[i]->handle;
            mLayers[i]->lastSourceCrop = mLayers[i]->sourceCrop;
            mLayers[i]->lastDisplayFrame = mLayers[i]->displayFrame;
            continue;
        }
        mLayerVector.add(mLayers[i]);
    }

    // If all layer is in overlay state,add ONLY_OVERLAY_BIT to mComposeFlag.
    // But only add it at the first time.
    lastComposeFlag &= ~ONLY_OVERLAY_MASK;
    if ( ((mComposeFlag & CLIENT_COMPOSE_MASK) ^ CLIENT_COMPOSE_MASK) &&
        mComposeFlag & OVERLAY_COMPOSE_MASK ) {
        if ( lastComposeFlag == mComposeFlag) {
            mComposeFlag &= ~ONLY_OVERLAY_MASK;
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
