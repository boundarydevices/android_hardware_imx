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
#include <dlfcn.h>
#include "Composer.h"
#include "MemoryManager.h"
#include <cutils/properties.h>

#if defined(__LP64__)
#define LIB_PATH1 "/system/lib64"
#define LIB_PATH2 "/vendor/lib64"
#else
#define LIB_PATH1 "/system/lib"
#define LIB_PATH2 "/vendor/lib"
#endif

#define GPUHELPER "libgpuhelper.so"
#define GPUENGINE "libg2d.so"

namespace fsl {

Composer* Composer::sInstance(0);
Mutex Composer::sLock(Mutex::PRIVATE);

Composer* Composer::getInstance()
{
    Mutex::Autolock _l(sLock);
    if (sInstance != NULL) {
        return sInstance;
    }

    sInstance = new Composer();
    return sInstance;
}

Composer::Composer()
{
    mTarget = NULL;
    mDimBuffer = NULL;
    mHelperHandle = NULL;
    mG2dHandle = NULL;

    char path[PATH_MAX] = {0};
    char value[PROPERTY_VALUE_MAX];
    mTls.tls = 0;
    mTls.has_tls = 0;
    pthread_mutex_init(&mTls.lock, NULL);

    property_get("sys.hwc.disable", value, "0");
    mDisableHWC = atoi(value);
    if (mDisableHWC) {
        ALOGI("HWC disabled!");
    }

    property_get("vendor.2d.composition", value, "1");
    m2DComposition = atoi(value);
    if (m2DComposition && !mDisableHWC) {
        ALOGI("g2d 2D composition enabled!");
    }
    else {
        ALOGI("Opengl ES 3D composition enabled!");
    }

    getModule(path, GPUHELPER);
    mHelperHandle = dlopen(path, RTLD_NOW);

    if (mHelperHandle == NULL) {
        ALOGV("no %s found", path);
        mGetAlignedSize = NULL;
        mGetFlipOffset = NULL;
        mGetTiling = NULL;
        mAlterFormat = NULL;
        mLockSurface = NULL;
        mUnlockSurface = NULL;
        mAlignTile = NULL;
    }
    else {
        mGetAlignedSize = (hwc_func3)dlsym(mHelperHandle, "hwc_getAlignedSize");
        mGetFlipOffset = (hwc_func2)dlsym(mHelperHandle, "hwc_getFlipOffset");
        mGetTiling = (hwc_func2)dlsym(mHelperHandle, "hwc_getTiling");
        mAlterFormat = (hwc_func2)dlsym(mHelperHandle, "hwc_alterFormat");
        mLockSurface = (hwc_func1)dlsym(mHelperHandle, "hwc_lockSurface");
        mUnlockSurface = (hwc_func1)dlsym(mHelperHandle, "hwc_unlockSurface");
        mAlignTile = (hwc_func4)dlsym(mHelperHandle, "hwc_align_tile");
    }

    memset(path, 0, sizeof(path));
    getModule(path, GPUENGINE);
    mG2dHandle = dlopen(path, RTLD_NOW);

    if (mG2dHandle == NULL) {
        ALOGI("can't find %s, 2D is invalid", path);
        mSetClipping = NULL;
        mBlitFunction = NULL;
        mOpenEngine = NULL;
        mCloseEngine = NULL;
        mClearFunction = NULL;
        mEnableFunction = NULL;
        mDisableFunction = NULL;
        mFinishEngine = NULL;
        mQueryFeature = NULL;
    }
    else {
        ALOGI("load %s library!", path);
        mSetClipping = (hwc_func5)dlsym(mG2dHandle, "g2d_set_clipping");
        mBlitFunction = (hwc_func3)dlsym(mG2dHandle, "g2d_blitEx");
        if (mBlitFunction == NULL) {
            mBlitFunction = (hwc_func3)dlsym(mG2dHandle, "g2d_blit");
        }
        mOpenEngine = (hwc_func1)dlsym(mG2dHandle, "g2d_open");
        mCloseEngine = (hwc_func1)dlsym(mG2dHandle, "g2d_close");
        mClearFunction = (hwc_func2)dlsym(mG2dHandle, "g2d_clear");
        mEnableFunction = (hwc_func2)dlsym(mG2dHandle, "g2d_enable");
        mDisableFunction = (hwc_func2)dlsym(mG2dHandle, "g2d_disable");
        mFinishEngine = (hwc_func1)dlsym(mG2dHandle, "g2d_finish");
        mQueryFeature = (hwc_func3)dlsym(mG2dHandle, "g2d_query_feature");
    }
}

Composer::~Composer()
{
    MemoryManager* pManager = MemoryManager::getInstance();
    if (mDimBuffer != NULL) {
        pManager->releaseMemory(mDimBuffer);
    }
    if (mG2dHandle != NULL) {
        dlclose(mG2dHandle);
    }
    if (mHelperHandle != NULL) {
        dlclose(mHelperHandle);
    }
}

void *Composer::getHandle()
{
    void *handle = thread_store_get(&mTls);
    if (handle != NULL) {
        return handle;
    }

    if (mOpenEngine == NULL) {
        return NULL;
    }

    handle = malloc(sizeof(void*));
    if (handle == NULL) {
        return NULL;
    }

    openEngine(&handle);
    thread_store_set(&mTls, handle, threadDestructor);
    return handle;
}

void Composer::threadDestructor(void *handle)
{
    if (handle == NULL) {
        return;
    }

    Composer::getInstance()->closeEngine(handle);
}

bool Composer::isValid()
{
    return (getHandle() != NULL && mBlitFunction != NULL);
}

bool Composer::isDisabled()
{
    return (mDisableHWC != 0);
}

bool Composer::is2DComposition()
{
    return (m2DComposition != 0);
}

void Composer::getModule(char *path, const char *name)
{
    snprintf(path, PATH_MAX, "%s/%s",
                                 LIB_PATH1, name);
    if (access(path, R_OK) == 0)
        return;
    snprintf(path, PATH_MAX, "%s/%s",
                                 LIB_PATH2, name);
    if (access(path, R_OK) == 0)
        return;
    return;
}

int Composer::checkDimBuffer()
{
    if (mTarget == NULL) {
        return 0;
    }

    if ((mDimBuffer != NULL) && (mTarget->width == mDimBuffer->width &&
        mTarget->height == mDimBuffer->height &&
        mTarget->fslFormat == mDimBuffer->fslFormat)) {
        return 0;
    }

    MemoryManager* pManager = MemoryManager::getInstance();
    if (mDimBuffer != NULL) {
        pManager->releaseMemory(mDimBuffer);
    }

    MemoryDesc desc;
    desc.mWidth = mTarget->width;
    desc.mHeight = mTarget->height;
    desc.mFormat = mTarget->format;
    desc.mFslFormat = mTarget->fslFormat;
    desc.mProduceUsage |= USAGE_HW_COMPOSER |
                          USAGE_HW_2D | USAGE_HW_RENDER |
                          USAGE_SW_WRITE_OFTEN | USAGE_SW_READ_OFTEN;
    desc.mFlag = FLAGS_DIMBUFFER;
    desc.checkFormat();
    int ret = pManager->allocMemory(desc, &mDimBuffer);
    if (ret == 0) {
        Rect rect;
        rect.left = rect.top = 0;
        rect.right = mTarget->width;
        rect.bottom = mTarget->height;
        clearRect(mDimBuffer, rect);
    }

    return ret;
}

int Composer::finishComposite()
{
    finishEngine(getHandle());
    return 0;
}

int Composer::setRenderTarget(Memory* memory)
{
    mTarget = memory;
    return 0;
}

int Composer::clearRect(Memory* target, Rect& rect)
{
    if (target == NULL || rect.isEmpty()) {
        return 0;
    }

    struct g2d_surfaceEx surfaceX;
    memset(&surfaceX, 0, sizeof(surfaceX));
    struct g2d_surface& surface = surfaceX.base;
    ALOGV("clearRect: rect(l:%d,t:%d,r:%d,b:%d)",
            rect.left, rect.top, rect.right, rect.bottom);
    setG2dSurface(surfaceX, target, rect);
    surface.clrcolor = 0xff << 24;
    clearFunction(getHandle(), &surface);

    return 0;
}

int Composer::clearWormHole(LayerVector& layers)
{
    if (mTarget == NULL) {
        ALOGE("clearWormHole: no effective render buffer");
        return -EINVAL;
    }

    // calculate opaque region.
    Region opaque;
    size_t count = layers.size();
    for (size_t i=0; i<count; i++) {
        Layer* layer = layers[i];
        if (!layer->busy){
            ALOGE("clearWormHole: compose invalid layer");
            continue;
        }

        if ((layer->blendMode == BLENDING_NONE) ||
             (i==0 && layer->blendMode == BLENDING_PREMULT) ||
             ((i!=0) && (layer->blendMode == BLENDING_DIM) &&
              ((layer->color >> 24)&0xff) == 0xff)) {
            opaque.orSelf(layer->visibleRegion);
        }
    }

    // calculate worm hole.
    Region screen(Rect(mTarget->width, mTarget->height));
    screen.subtractSelf(opaque);
    const Rect *holes = NULL;
    size_t numRect = 0;
    holes = screen.getArray(&numRect);
    // clear worm hole.
    struct g2d_surfaceEx surfaceX;
    memset(&surfaceX, 0, sizeof(surfaceX));
    struct g2d_surface& surface = surfaceX.base;
    for (size_t i=0; i<numRect; i++) {
        if (holes[i].isEmpty()) {
            continue;
        }

        Rect& rect = (Rect&)holes[i];
        ALOGV("clearhole: hole(l:%d,t:%d,r:%d,b:%d)",
                rect.left, rect.top, rect.right, rect.bottom);
        setG2dSurface(surfaceX, mTarget, rect);
        surface.clrcolor = 0xff << 24;
        clearFunction(getHandle(), &surface);
    }

    return 0;
}

int Composer::composeLayer(Layer* layer, bool bypass)
{
    if (layer == NULL || mTarget == NULL) {
        ALOGE("composeLayer: invalid layer or target");
        return -EINVAL;
    }

    if (bypass && layer->isSolidColor()) {
        ALOGV("composeLayer dim layer bypassed");
        return 0;
    }

    Rect srect = layer->sourceCrop;
    Rect drect = layer->displayFrame;
    struct g2d_surfaceEx dSurfaceX;
    struct g2d_surface& dSurface = dSurfaceX.base;

    if ((srect.isEmpty() && !layer->isSolidColor())
         || drect.isEmpty()) {
        ALOGE("composeLayer: invalid srect or drect");
        return 0;
    }

    if (layer->isSolidColor()) {
        checkDimBuffer();
    }

    memset(&dSurfaceX, 0, sizeof(dSurfaceX));
    size_t count = 0;
    const Rect* visible = layer->visibleRegion.getArray(&count);
    for (size_t i=0; i<count; i++) {
        Rect srect = layer->sourceCrop;
        Rect clip = visible[i];
        if (clip.isEmpty()) {
            ALOGV("composeLayer: invalid clip");
            continue;
        }

        clip.intersect(drect, &clip);
        if (clip.isEmpty()) {
            ALOGV("composeLayer: invalid clip rect");
            continue;
        }

        setClipping(srect, drect, clip, layer->transform);
        ALOGV("index:%d, i:%d sourceCrop(l:%d,t:%d,r:%d,b:%d), "
             "visible(l:%d,t:%d,r:%d,b:%d), "
             "display(l:%d,t:%d,r:%d,b:%d)", layer->index, (int)i,
             srect.left, srect.top, srect.right, srect.bottom,
             clip.left, clip.top, clip.right, clip.bottom,
             drect.left, drect.top, drect.right, drect.bottom);
        if (layer->handle != nullptr) {
            ALOGV("zorder:0x%x, layer phys:0x%" PRIx64,
                layer->zorder, layer->handle->phys);
        }
        ALOGV("transform:0x%x, blend:0x%x, alpha:0x%x",
                layer->transform, layer->blendMode, layer->planeAlpha);

        setG2dSurface(dSurfaceX, mTarget, drect);

        struct g2d_surfaceEx sSurfaceX;
        memset(&sSurfaceX, 0, sizeof(sSurfaceX));
        struct g2d_surface& sSurface = sSurfaceX.base;

        if (!layer->isSolidColor() && layer->handle) {
            setG2dSurface(sSurfaceX, layer->handle, srect);
        }
        else if (mDimBuffer) {
            setG2dSurface(sSurfaceX, mDimBuffer, drect);
        }
        else {
            return -EINVAL;
        }

        convertRotation(layer->transform, sSurface, dSurface);
        if (!bypass) {
            convertBlending(layer->blendMode, sSurface, dSurface);
        }
        sSurface.global_alpha = layer->planeAlpha;

        if (layer->blendMode != BLENDING_NONE && !bypass) {
            enableFunction(getHandle(), G2D_GLOBAL_ALPHA, true);
            enableFunction(getHandle(), G2D_BLEND, true);
        }

        blitSurface(&sSurfaceX, &dSurfaceX);

        if (layer->blendMode != BLENDING_NONE && !bypass) {
            enableFunction(getHandle(), G2D_BLEND, false);
            enableFunction(getHandle(), G2D_GLOBAL_ALPHA, false);
        }
    }

    return 0;
}

int Composer::setG2dSurface(struct g2d_surfaceEx& surfaceX, Memory *handle, Rect& rect)
{
    int alignWidth = 0, alignHeight = 0;
    struct g2d_surface& surface = surfaceX.base;

    int ret = getAlignedSize(handle, NULL, &alignHeight);
    if (ret != 0) {
        alignHeight = handle->height;
    }

    alignWidth = handle->stride;
    surface.format = convertFormat(handle->fslFormat, handle);
    surface.stride = alignWidth;
    enum g2d_tiling tile = G2D_LINEAR;
    getTiling(handle, &tile);
    if (handle->fslFormat == FORMAT_NV12_TILED) {
        surfaceX.tiling = G2D_AMPHION_TILED;
    }
    else {
        surfaceX.tiling = tile;
    }

    int offset = 0;
    getFlipOffset(handle, &offset);
    surface.planes[0] = (int)handle->phys + offset;

    switch (surface.format) {
        case G2D_RGB565:
        case G2D_YUYV:
        case G2D_RGBA8888:
        case G2D_BGRA8888:
        case G2D_RGBX8888:
        case G2D_BGRX8888:
            break;

        case G2D_NV16:
        case G2D_NV12:
        case G2D_NV21:
            surface.planes[1] = surface.planes[0] + surface.stride * alignHeight;
            break;

        case G2D_I420:
        case G2D_YV12: {
            int c_stride = (alignWidth/2+15)/16*16;
            int stride = alignWidth;

            surface.stride = alignWidth;
            if (surface.format == G2D_I420) {
                surface.planes[1] = surface.planes[0] + stride * handle->height;
                surface.planes[2] = surface.planes[1] + c_stride * handle->height/2;
            }
            else {
                surface.planes[2] = surface.planes[0] + stride * handle->height;
                surface.planes[1] = surface.planes[2] + c_stride * handle->height/2;
            }
            } break;

        default:
            ALOGI("does not support format:%d", surface.format);
            break;
    }
    surface.left = rect.left;
    surface.top = rect.top;
    surface.right = rect.right;
    surface.bottom = rect.bottom;
    surface.width = handle->width;
    surface.height = handle->height;

    return 0;
}

enum g2d_format Composer::convertFormat(int format, Memory *handle)
{
    enum g2d_format halFormat;
    switch (format) {
        case FORMAT_RGBA8888:
            halFormat = G2D_RGBA8888;
            break;
        case FORMAT_RGBX8888:
            halFormat = G2D_RGBX8888;
            break;
        case FORMAT_RGB565:
            halFormat = G2D_RGB565;
            break;
        case FORMAT_BGRA8888:
            halFormat = G2D_BGRA8888;
            break;

        case FORMAT_NV21:
            halFormat = G2D_NV21;
            break;
        case FORMAT_NV12:
        case FORMAT_NV12_TILED:
            halFormat = G2D_NV12;
            break;

        case FORMAT_I420:
            halFormat = G2D_I420;
            break;
        case FORMAT_YV12:
            halFormat = G2D_YV12;
            break;

        case FORMAT_NV16:
            halFormat = G2D_NV16;
            break;
        case FORMAT_YUYV:
            halFormat = G2D_YUYV;
            break;

        default:
            ALOGE("unsupported format:0x%x", format);
            halFormat = G2D_RGBA8888;
            break;
    }

    halFormat = alterFormat(handle, halFormat);
    return halFormat;
}

int Composer::convertRotation(int transform, struct g2d_surface& src,
                        struct g2d_surface& dst)
{
    switch (transform) {
        case 0:
            dst.rot = G2D_ROTATION_0;
            break;
        case TRANSFORM_ROT90:
            dst.rot =  G2D_ROTATION_90;
            break;
        case TRANSFORM_FLIPH | TRANSFORM_FLIPV:
            dst.rot =  G2D_ROTATION_180;
            break;
        case TRANSFORM_FLIPH | TRANSFORM_FLIPV
             | HAL_TRANSFORM_ROT_90:
            dst.rot =  G2D_ROTATION_270;
            break;
        case TRANSFORM_FLIPH:
            dst.rot =  G2D_FLIP_H;
            break;
        case TRANSFORM_FLIPV:
            dst.rot =  G2D_FLIP_V;
            break;
        case TRANSFORM_FLIPH | TRANSFORM_ROT90:
            dst.rot =  G2D_ROTATION_90;
            src.rot =  G2D_FLIP_H;
            break;
        case TRANSFORM_FLIPV | TRANSFORM_ROT90:
            dst.rot =  G2D_ROTATION_90;
            src.rot =  G2D_FLIP_V;
            break;
        default:
            dst.rot =  G2D_ROTATION_0;
            break;
    }

    return 0;
}

int Composer::convertBlending(int blending, struct g2d_surface& src,
                        struct g2d_surface& dst)
{
    switch (blending) {
        case BLENDING_PREMULT:
            src.blendfunc = G2D_ONE;
            dst.blendfunc = G2D_ONE_MINUS_SRC_ALPHA;
            break;

        case BLENDING_COVERAGE:
            src.blendfunc = G2D_SRC_ALPHA;
            dst.blendfunc = G2D_ONE_MINUS_SRC_ALPHA;
            break;

        case BLENDING_DIM:
            src.blendfunc = G2D_ONE;
            dst.blendfunc = G2D_ONE_MINUS_SRC_ALPHA;
            break;

        default:
            src.blendfunc = G2D_ONE;
            dst.blendfunc = G2D_ONE_MINUS_SRC_ALPHA;
            break;
    }

    return 0;
}

int Composer::getAlignedSize(Memory *handle, int *width, int *height)
{
    if (mGetAlignedSize == NULL) {
        return -EINVAL;
    }

    return (*mGetAlignedSize)(handle, (void*)width, (void*)height);
}

int Composer::getFlipOffset(Memory *handle, int *offset)
{
    if (mGetFlipOffset == NULL) {
        return -EINVAL;
    }

    return (*mGetFlipOffset)(handle, (void*)offset);
}

int Composer::getTiling(Memory *handle, enum g2d_tiling* tile)
{
    if (mGetTiling == NULL) {
        return -EINVAL;
    }

    return (*mGetTiling)(handle, (void*)tile);
}

enum g2d_format Composer::alterFormat(Memory *handle, enum g2d_format format)
{
    if (mAlterFormat == NULL) {
        return format;
    }

    return (enum g2d_format)(*mAlterFormat)(handle, (void*)format);
}

int Composer::lockSurface(Memory *handle)
{
    if (mLockSurface == NULL) {
        return -EINVAL;
    }

    return (*mLockSurface)(handle);
}

int Composer::unlockSurface(Memory *handle)
{
    if (mUnlockSurface == NULL) {
        return -EINVAL;
    }

    return (*mUnlockSurface)(handle);
}

int Composer::setClipping(Rect& /*src*/, Rect& /*dst*/, Rect& clip, int /*rotation*/)
{
    if (mSetClipping == NULL) {
        return -EINVAL;
    }

    return (*mSetClipping)(getHandle(), (void*)(intptr_t)clip.left,
            (void*)(intptr_t)clip.top, (void*)(intptr_t)clip.right,
            (void*)(intptr_t)clip.bottom);
}

int Composer::blitSurface(struct g2d_surfaceEx *srcEx, struct g2d_surfaceEx *dstEx)
{
    if (mBlitFunction == NULL) {
        return -EINVAL;
    }

    return (*mBlitFunction)(getHandle(), srcEx, dstEx);
}

int Composer::openEngine(void** handle)
{
    if (mOpenEngine == NULL) {
        return -EINVAL;
    }

    return (*mOpenEngine)((void*)handle);
}

int Composer::closeEngine(void* handle)
{
    if (mCloseEngine == NULL) {
        return -EINVAL;
    }

    return (*mCloseEngine)(handle);
}

int Composer::clearFunction(void* handle, struct g2d_surface* area)
{
    if (mClearFunction == NULL) {
        return -EINVAL;
    }

    return (*mClearFunction)(handle, area);
}

int Composer::enableFunction(void* handle, enum g2d_cap_mode cap, bool enable)
{
    if (mEnableFunction == NULL || mDisableFunction == NULL) {
        return -EINVAL;
    }

    int ret = 0;
    if (enable) {
        ret = (*mEnableFunction)(handle, (void*)cap);
    }
    else {
        ret = (*mDisableFunction)(handle, (void*)cap);
    }

    return ret;
}

int Composer::finishEngine(void* handle)
{
    if (mFinishEngine == NULL) {
        return -EINVAL;
    }

    return (*mFinishEngine)(handle);
}

bool Composer::isFeatureSupported(g2d_feature feature)
{
    if (mQueryFeature == NULL || getHandle() == NULL) {
        return false;
    }

    int enable = 0;
    (*mQueryFeature)(getHandle(), (void*)feature, (void*)&enable);
    return (enable != 0);
}

int Composer::alignTile(int *width, int *height, int format, int usage)
{
    if (mAlignTile == NULL) {
        return -EINVAL;
    }
    return (*mAlignTile)(width, height, (void*)(intptr_t)format, (void*)(intptr_t)usage);
}

}
