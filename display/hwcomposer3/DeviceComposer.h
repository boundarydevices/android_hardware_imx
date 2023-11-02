/*
 * Copyright 2017-2023 NXP.
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

#ifndef _DEVICE_COMPOSER_H_
#define _DEVICE_COMPOSER_H_

#include <g2dExt.h>
#include <utils/threads.h>

#include "Layer.h"
#include "gralloc_handle.h"

namespace aidl::android::hardware::graphics::composer3::impl {

typedef int (*hwc_func1)(void* handle);
typedef int (*hwc_func2)(void* handle, void* arg1);
typedef int (*hwc_func3)(void* handle, void* arg1, void* arg2);
typedef int (*hwc_func4)(void* handle, void* arg1, void* arg2, void* arg3);
typedef int (*hwc_func5)(void* handle, void* arg1, void* arg2, void* arg3, void* arg4);

using ::android::Mutex;

class DeviceComposer {
public:
    DeviceComposer();
    ~DeviceComposer();

    bool isValid();
    int alignTile(int* width, int* height, int format, int usage);

    bool checkMustDeviceComposition(Layer* layer);
    bool checkDeviceComposition(Layer* layer);
    int prepareDeviceFrameBuffer(HalDisplayConfig& config, uint32_t uiType,
                                 gralloc_handle_t* buffers, int count, bool secure);
    int freeDeviceFrameBuffer(gralloc_handle_t buffers[], int count);

    bool composeLayers(std::vector<Layer*> layers, buffer_handle_t target);

private:
    void* getHandle();

    // set composite target buffer.
    int setRenderTarget(gralloc_handle_t memory);
    // clear worm hole introduced by layers not cover whole screen.
    int clearWormHole(std::vector<Layer*>& layers);
    // compose display layer.
    int composeLayerLocked(Layer* layer, bool bypass);
    // sync 2D blit engine.
    int finishComposite();
    // lock surface to get GPU specific resource.
    int lockSurface(gralloc_handle_t handle);
    // unlock surface to release resource.
    int unlockSurface(gralloc_handle_t handle);
    bool isFeatureSupported(g2d_feature feature);

    int setG2dSurface(struct g2d_surfaceEx& surfaceX, gralloc_handle_t handle, common::Rect& rect);
    enum g2d_format convertFormat(int format, gralloc_handle_t handle);
    int convertRotation(common::Transform transform, struct g2d_surface& src,
                        struct g2d_surface& dst);
    int convertBlending(common::BlendMode blending, struct g2d_surface& src,
                        struct g2d_surface& dst);
    int prepareSolidColorBuffer();
    int clearRect(gralloc_handle_t target, common::Rect& rect);

    int getAlignedSize(gralloc_handle_t handle, int* width, int* height);
    int getFlipOffset(gralloc_handle_t handle, int* offset);
    int getTiling(gralloc_handle_t handle, enum g2d_tiling* tile);
    int getTileStatus(gralloc_handle_t handle, struct g2d_surfaceEx* surfaceX);
    int resolveTileStatus(gralloc_handle_t handle);
    enum g2d_format alterFormat(gralloc_handle_t handle, enum g2d_format format);

    int setClipping(common::Rect& src, common::Rect& dst, common::Rect& clip,
                    common::Transform rotation);
    int blitSurface(struct g2d_surfaceEx* srcEx, struct g2d_surfaceEx* dstEx);
    int openEngine(void** handle);
    int closeEngine(void* handle);
    int clearFunction(void* handle, struct g2d_surface* area);
    int enableFunction(void* handle, enum g2d_cap_mode cap, bool enable);
    int finishEngine(void* handle);

private:
    static Mutex sLock;
    static thread_local void* sHandle;

    bool mG2dPrefered;
    ;

    gralloc_handle_t mTarget = NULL;
    gralloc_handle_t mSolidColorBuffer = NULL;

    hwc_func3 mGetAlignedSize;
    hwc_func2 mGetFlipOffset;
    hwc_func2 mGetTiling;
    hwc_func2 mAlterFormat;
    hwc_func1 mLockSurface;
    hwc_func1 mUnlockSurface;

    hwc_func5 mSetClipping;
    hwc_func3 mBlitFunction;
    hwc_func1 mOpenEngine;
    hwc_func1 mCloseEngine;
    hwc_func2 mClearFunction;
    hwc_func2 mEnableFunction;
    hwc_func2 mDisableFunction;
    hwc_func1 mFinishEngine;
    hwc_func3 mQueryFeature;
    hwc_func4 mAlignTile;
    hwc_func2 mGetTileStatus;
    hwc_func1 mResolveTileStatus;

    void* mHelperHandle = NULL;
    void* mG2dHandle = NULL;
};

} // namespace aidl::android::hardware::graphics::composer3::impl
#endif
