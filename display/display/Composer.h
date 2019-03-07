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

#ifndef _FSL_COMPOSER_H_
#define _FSL_COMPOSER_H_

#include <utils/threads.h>
#include <cutils/threads.h>
#include <g2dExt.h>
#include "Memory.h"
#include "Layer.h"

namespace fsl {

typedef int (*hwc_func1)(void* handle);
typedef int (*hwc_func2)(void* handle, void* arg1);
typedef int (*hwc_func3)(void* handle, void* arg1, void* arg2);
typedef int (*hwc_func4)(void* handle, void* arg1, void* arg2, void* arg3);
typedef int (*hwc_func5)(void* handle, void* arg1, void* arg2, void* arg3, void* arg4);

using android::Mutex;

class Composer
{
public:
    ~Composer();
    static Composer* getInstance();

    bool isValid();
    // check hwc disabled or not.
    bool isDisabled();
    // check 2d composition or not.
    bool is2DComposition();
    // set composite target buffer.
    int setRenderTarget(Memory* memory);
    // clear worm hole introduced by layers not cover whole screen.
    int clearWormHole(LayerVector& layers);
    // compose display layer.
    int composeLayer(Layer* layer, bool bypass);
    // sync 2D blit engine.
    int finishComposite();
    // lock surface to get GPU specific resource.
    int lockSurface(Memory *handle);
    // unlock surface to release resource.
    int unlockSurface(Memory *handle);
    bool isFeatureSupported(g2d_feature feature);
    int alignTile(int *width, int *height, int format, int usage);

private:
    Composer();
    void *getHandle();
    static void threadDestructor(void *handle);
    int setG2dSurface(struct g2d_surfaceEx& surfaceX, Memory *handle, Rect& rect);
    enum g2d_format convertFormat(int format, Memory *handle);
    int convertRotation(int transform, struct g2d_surface& src,
                        struct g2d_surface& dst);
    int convertBlending(int blending, struct g2d_surface& src,
                        struct g2d_surface& dst);
	void getModule(char *path, const char *name);
    int checkDimBuffer();
    int clearRect(Memory* target, Rect& rect);

    int getAlignedSize(Memory *handle, int *width, int *height);
    int getFlipOffset(Memory *handle, int *offset);
    int getTiling(Memory *handle, enum g2d_tiling* tile);
    enum g2d_format alterFormat(Memory *handle, enum g2d_format format);

    int setClipping(Rect& src, Rect& dst, Rect& clip, int rotation);
    int blitSurface(struct g2d_surfaceEx *srcEx, struct g2d_surfaceEx *dstEx);
    int openEngine(void** handle);
    int closeEngine(void* handle);
    int clearFunction(void* handle, struct g2d_surface* area);
    int enableFunction(void* handle, enum g2d_cap_mode cap, bool enable);
    int finishEngine(void* handle);

private:
    static Mutex sLock;
    static Composer* sInstance;

    thread_store_t mTls;
    Memory* mTarget;
    Memory* mDimBuffer;

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

    int mDisableHWC;
    int m2DComposition;

    void* mHelperHandle;
    void* mG2dHandle;
};

}
#endif
