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

#include <cutils/log.h>
#include <sync/sync.h>

#include "Memory.h"
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

int Display::setPowerMode(int mode)
{
    return -1;
}

void Display::setVsyncEnabled(bool enabled)
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

bool Display::checkOverlay(Layer* layer)
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

bool Display::verifyLayers()
{
    bool deviceCompose = true;
    bool rotationCap = mComposer.isFeatureSupported(G2D_ROTATION);

    Mutex::Autolock _l(mLock);
    mLayerVector.clear();
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

    if (!mComposer.isValid()) {
        deviceCompose = false;
    }

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
            continue;
        }
        mLayerVector.add(mLayers[i]);
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

int Display::composeLayers()
{
    Mutex::Autolock _l(mLock);

    return composeLayersLocked();
}

void Display::waitOnFenceLocked()
{
    // release target fence.
    if (mAcquireFence != -1) {
        sync_wait(mAcquireFence, -1);
        close(mAcquireFence);
        mAcquireFence = -1;
    }

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

int Display::composeLayersLocked()
{
    int ret = 0;

    waitOnFenceLocked();

    if (!mConnected && mIndex != DISPLAY_PRIMARY) {
        ALOGE("composeLayersLocked display plugout");
        return -EINVAL;
    }

    performOverlay();

    if (mLayerVector.size() <= 0) {
        return ret;
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

        if (layer->handle != NULL)
            mComposer.lockSurface(layer->handle);

        ret = mComposer.composeLayer(layer, i==0);

        if (layer->handle != NULL)
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

}
