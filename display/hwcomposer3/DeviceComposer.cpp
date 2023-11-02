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
#include "DeviceComposer.h"

#include <cutils/properties.h>
#include <dlfcn.h>
#include <hardware/gralloc.h>
#include <inttypes.h>
#include <ui/GraphicBufferAllocator.h>
#include <ui/Rect.h>
#include <ui/Region.h>
#include <vndksupport/linker.h>

#include "Common.h"
#include "Drm.h"

#define GPUHELPER "libgpuhelper.so"
#define G2DENGINE "libg2d"

namespace aidl::android::hardware::graphics::composer3::impl {

Mutex DeviceComposer::sLock(Mutex::PRIVATE);
thread_local void* DeviceComposer::sHandle(0);

static bool getDefaultG2DLib(char* libName, uint32_t size) {
    char value[PROPERTY_VALUE_MAX];

    if ((libName == NULL) || (size < strlen(G2DENGINE) + strlen(".so")))
        return false;

    memset(libName, 0, size);
    property_get("vendor.imx.default-g2d", value, "");
    if (strcmp(value, "") == 0) {
        ALOGI("No g2d lib available to be used!");
        return false;
    } else {
        strncpy(libName, G2DENGINE, strlen(G2DENGINE));
        strcat(libName, "-");
        strcat(libName, value);
        strcat(libName, ".so");
    }
    ALOGI("Default g2d lib: %s", libName);
    return true;
}

DeviceComposer::DeviceComposer() {
    mTarget = NULL;
    mSolidColorBuffer = NULL;
    mHelperHandle = NULL;
    mG2dHandle = NULL;

    char g2dlibName[PATH_MAX] = {0};

    mG2dPrefered = Is2DCompositionUserPrefered();
    if (mG2dPrefered) {
        ALOGI("%s: Prefer to use g2d/dpu 2D composition!", __FUNCTION__);
    } else {
        ALOGI("%s: Prefer to use Opengl ES 3D composition!", __FUNCTION__);
    }

    mHelperHandle = android_load_sphal_library(GPUHELPER, RTLD_LOCAL | RTLD_NOW);
    if (mHelperHandle == NULL) {
        ALOGE("fail to open libgpuhelper.so");
        mGetAlignedSize = NULL;
        mGetFlipOffset = NULL;
        mGetTiling = NULL;
        mAlterFormat = NULL;
        mLockSurface = NULL;
        mUnlockSurface = NULL;
        mAlignTile = NULL;
        mGetTileStatus = NULL;
        mResolveTileStatus = NULL;
    } else {
        mGetAlignedSize = (hwc_func3)dlsym(mHelperHandle, "hwc_getAlignedSize");
        mGetFlipOffset = (hwc_func2)dlsym(mHelperHandle, "hwc_getFlipOffset");
        mGetTiling = (hwc_func2)dlsym(mHelperHandle, "hwc_getTiling");
        mAlterFormat = (hwc_func2)dlsym(mHelperHandle, "hwc_alterFormat");
        mLockSurface = (hwc_func1)dlsym(mHelperHandle, "hwc_lockSurface");
        mUnlockSurface = (hwc_func1)dlsym(mHelperHandle, "hwc_unlockSurface");
        mAlignTile = (hwc_func4)dlsym(mHelperHandle, "hwc_align_tile");
        mGetTileStatus = (hwc_func2)dlsym(mHelperHandle, "hwc_get_tileStatus");
        mResolveTileStatus = (hwc_func1)dlsym(mHelperHandle, "hwc_resolve_tileStatus");
    }

    if (getDefaultG2DLib(g2dlibName, PATH_MAX)) {
        mG2dHandle = android_load_sphal_library(g2dlibName, RTLD_LOCAL | RTLD_NOW);
    }

    if (mG2dHandle == NULL) {
        ALOGI("can't find %s, 2D is invalid", g2dlibName);
        mSetClipping = NULL;
        mBlitFunction = NULL;
        mOpenEngine = NULL;
        mCloseEngine = NULL;
        mClearFunction = NULL;
        mEnableFunction = NULL;
        mDisableFunction = NULL;
        mFinishEngine = NULL;
        mQueryFeature = NULL;
    } else {
        ALOGI("load %s library successfully!", g2dlibName);
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

DeviceComposer::~DeviceComposer() {
    if (mSolidColorBuffer != NULL) {
        unlockSurface(mSolidColorBuffer);
        ::android::GraphicBufferAllocator::get().free(mSolidColorBuffer);
    }
    if (mG2dHandle != NULL) {
        dlclose(mG2dHandle);
    }
    if (mHelperHandle != NULL) {
        dlclose(mHelperHandle);
    }
    if (sHandle != NULL) {
        closeEngine(sHandle);
    }
}

void* DeviceComposer::getHandle() {
    if (sHandle != NULL) {
        return sHandle;
    }

    if (mOpenEngine == NULL) {
        return NULL;
    }

    openEngine(&sHandle);
    return sHandle;
}

bool DeviceComposer::isValid() {
    return (getHandle() != NULL && mBlitFunction != NULL);
}

int DeviceComposer::prepareDeviceFrameBuffer(HalDisplayConfig& config, uint32_t uiType,
                                             gralloc_handle_t* buffers, int count, bool secure) {
    uint32_t width, height;
    uint32_t bufferStride;
    uint64_t usage;
    buffer_handle_t bufferHandle;
    int format = static_cast<int>(common::PixelFormat::RGBA_8888);

    if (uiType == UI_SCALE_SOFTWARE) {
        width = config.modeWidth;
        height = config.modeHeight;
    } else {
        width = config.width;
        height = config.height;
    }

    usage = GRALLOC_USAGE_HW_FB | GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_COMPOSER |
            GRALLOC_USAGE_HW_2D;
    if (secure)
        usage |= GRALLOC_USAGE_PROTECTED;

    for (int i = 0; i < count; i++) {
        auto status = ::android::GraphicBufferAllocator::get().allocate(width, height, format,
                                                                        /*layerCount=*/1, usage,
                                                                        &bufferHandle,
                                                                        &bufferStride, "NxpHwc");
        if (status != ::android::OK) {
            ALOGE("%s: failed to allocate buffer:%d x %d, format=%x, usage=%lx, ret=%d",
                  __FUNCTION__, width, height, format, usage, status);
            return status;
        }

        buffers[i] = (gralloc_handle_t)bufferHandle;
    }

    return 0;
}

int DeviceComposer::freeDeviceFrameBuffer(gralloc_handle_t buffers[], int count) {
    for (int i = 0; i < count; i++) {
        ::android::GraphicBufferAllocator::get().free(buffers[i]);
    }

    return 0;
}

int DeviceComposer::prepareSolidColorBuffer() {
    if (mTarget == NULL) {
        return 0;
    }

    if ((mSolidColorBuffer != NULL) &&
        (mTarget->width == mSolidColorBuffer->width &&
         mTarget->height == mSolidColorBuffer->height &&
         mTarget->fslFormat == mSolidColorBuffer->fslFormat)) {
        return 0;
    }

    if (mSolidColorBuffer != NULL) {
        unlockSurface(mSolidColorBuffer);
        ::android::GraphicBufferAllocator::get().free(mSolidColorBuffer);
    }

    uint32_t bufferStride;
    buffer_handle_t bufferHandle;

    auto status =
            ::android::GraphicBufferAllocator::get().allocate(mTarget->width, mTarget->height,
                                                              mTarget->format, /*layerCount=*/1,
                                                              GRALLOC_USAGE_HW_RENDER |
                                                                      GRALLOC_USAGE_HW_COMPOSER |
                                                                      GRALLOC_USAGE_HW_2D |
                                                                      GRALLOC_USAGE_SW_READ_OFTEN |
                                                                      GRALLOC_USAGE_SW_WRITE_OFTEN,
                                                              &bufferHandle, &bufferStride,
                                                              "NxpHwc");
    if (status != ::android::OK) {
        ALOGE("%s: failed to allocate solid color buffer", __FUNCTION__);
        return -1;
    }

    mSolidColorBuffer = (gralloc_handle_t)bufferHandle;

    return 0;
}

int DeviceComposer::finishComposite() {
    finishEngine(getHandle());
    return 0;
}

int DeviceComposer::setRenderTarget(gralloc_handle_t memory) {
    mTarget = memory;
    return 0;
}

int DeviceComposer::clearRect(gralloc_handle_t target, common::Rect& rect) {
    if (target == NULL || isRectEmpty(rect)) {
        return 0;
    }

    struct g2d_surfaceEx surfaceX;
    struct g2d_surface& surface = surfaceX.base;

    memset(&surfaceX, 0, sizeof(surfaceX));
    setG2dSurface(surfaceX, target, rect);
    surface.clrcolor = 0xff << 24;
    clearFunction(getHandle(), &surface);

    ALOGV("clearRect: rect(l:%d,t:%d,r:%d,b:%d)", rect.left, rect.top, rect.right, rect.bottom);
    return 0;
}

int DeviceComposer::clearWormHole(std::vector<Layer*>& layers) {
    if (mTarget == NULL) {
        ALOGE("%s: no effective render buffer", __FUNCTION__);
        return -EINVAL;
    }

    // calculate opaque region.
    int i = 0;
    ::android::Region opaque;
    for (auto layer : layers) {
        auto mode = layer->getBlendMode();
        auto type = layer->getCompositionType();
        auto color = layer->getColor();
        if ((mode == common::BlendMode::NONE) ||
            (i == 0 && mode == common::BlendMode::PREMULTIPLIED) ||
            ((i != 0) && (type == Composition::SOLID_COLOR) &&
             (std::fabs(color.a - 1.0f) < 1e-9))) {
            for (auto& rect : layer->getVisibleRegion()) {
                opaque.orSelf(::android::Rect(rect.left, rect.top, rect.right, rect.bottom));
            }
        }
        i++;
    }

    // calculate worm hole.
    ::android::Region screen(::android::Rect(mTarget->width, mTarget->height));
    screen.subtractSelf(opaque);
    const ::android::Rect* holes = NULL;
    size_t numRect = 0;
    holes = screen.getArray(&numRect);
    // clear worm hole.
    struct g2d_surfaceEx surfaceX;
    memset(&surfaceX, 0, sizeof(surfaceX));
    struct g2d_surface& surface = surfaceX.base;
    for (size_t i = 0; i < numRect; i++) {
        if (holes[i].isEmpty()) {
            continue;
        }

        common::Rect rect;
        rect.left = holes[i].left;
        rect.top = holes[i].top;
        rect.right = holes[i].right;
        rect.bottom = holes[i].bottom;
        ALOGV("clearhole: hole(l:%d,t:%d,r:%d,b:%d)", rect.left, rect.top, rect.right, rect.bottom);
        setG2dSurface(surfaceX, mTarget, rect);
        surface.clrcolor = 0xff << 24;
        clearFunction(getHandle(), &surface);
    }

    return 0;
}

int DeviceComposer::composeLayerLocked(Layer* layer, bool bypass) {
    if (layer == NULL || mTarget == NULL) {
        ALOGE("%s: invalid layer or target", __FUNCTION__);
        return -EINVAL;
    }

    auto type = layer->getCompositionType();
    auto mode = layer->getBlendMode();
    auto transform = layer->getTransform();
    auto alpha = (uint8_t)(layer->getPlaneAlpha() * 255);
    gralloc_handle_t layerBuffer = (gralloc_handle_t)(layer->getBuffer().getBuffer());

    if (bypass && (type == Composition::SOLID_COLOR)) {
        ALOGV("%s: solid color layer bypassed", __FUNCTION__);
        return 0;
    }

    common::Rect srect = layer->getSourceCropInt();
    common::Rect drect = layer->getDisplayFrame();
    struct g2d_surfaceEx dSurfaceX;
    struct g2d_surface& dSurface = dSurfaceX.base;

    if ((isRectEmpty(srect) && !(type == Composition::SOLID_COLOR)) || isRectEmpty(drect)) {
        ALOGE("%s: invalid srect or drect", __FUNCTION__);
        return 0;
    }

    if (type == Composition::SOLID_COLOR) {
        prepareSolidColorBuffer();
    }

    memset(&dSurfaceX, 0, sizeof(dSurfaceX));
    bool needDither = false;
    std::vector<common::Rect>& visible = layer->getVisibleRegion();
    for (auto& clip : visible) {
        if (isRectEmpty(clip)) {
            ALOGV("%s: invalid clip", __FUNCTION__);
            continue;
        }

        if (!rectIntersect(drect, clip)) {
            ALOGV("%s: invalid clip rect", __FUNCTION__);
            continue;
        }

        setClipping(srect, drect, clip, transform);
        ALOGV("index:%ld, sourceCrop(l:%d,t:%d,r:%d,b:%d), visible(l:%d,t:%d,r:%d,b:%d), "
              "display(l:%d,t:%d,r:%d,b:%d)",
              layer->getId(), srect.left, srect.top, srect.right, srect.bottom, clip.left, clip.top,
              clip.right, clip.bottom, drect.left, drect.top, drect.right, drect.bottom);

        if (layerBuffer != nullptr)
            ALOGV("zorder:0x%x, layer phys:0x%" PRIx64, layer->getZOrder(), layerBuffer->phys);

        ALOGV("transform:0x%x, blend:0x%x, alpha:0x%x", transform, mode, alpha);

        setG2dSurface(dSurfaceX, mTarget, drect);

        struct g2d_surfaceEx sSurfaceX;
        memset(&sSurfaceX, 0, sizeof(sSurfaceX));
        struct g2d_surface& sSurface = sSurfaceX.base;

        if (!(type == Composition::SOLID_COLOR) && layerBuffer) {
            setG2dSurface(sSurfaceX, layerBuffer, srect);
            if ((mTarget->fslFormat == FORMAT_RGB565) &&
                (layerBuffer->fslFormat == FORMAT_RGBA8888 ||
                 layerBuffer->fslFormat == FORMAT_RGBX8888 ||
                 layerBuffer->fslFormat == FORMAT_BGRA8888)) {
                needDither = true;
            }

        } else if (mSolidColorBuffer) {
            setG2dSurface(sSurfaceX, mSolidColorBuffer, drect);
        } else {
            return -EINVAL;
        }

        convertRotation(transform, sSurface, dSurface);
        if (!bypass)
            convertBlending(mode, sSurface, dSurface);

        sSurface.global_alpha = alpha;

        if ((mode != common::BlendMode::NONE) && !bypass) {
            enableFunction(getHandle(), G2D_GLOBAL_ALPHA, true);
            enableFunction(getHandle(), G2D_BLEND, true);
        }

        if (needDither)
            enableFunction(getHandle(), G2D_DITHER, true);

        blitSurface(&sSurfaceX, &dSurfaceX);

        if (needDither)
            enableFunction(getHandle(), G2D_DITHER, false);

        if ((mode != common::BlendMode::NONE) && !bypass) {
            enableFunction(getHandle(), G2D_BLEND, false);
            enableFunction(getHandle(), G2D_GLOBAL_ALPHA, false);
        }
    }

    return 0;
}

int DeviceComposer::setG2dSurface(struct g2d_surfaceEx& surfaceX, gralloc_handle_t handle,
                                  common::Rect& rect) {
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
    } else {
        surfaceX.tiling = tile;
    }

    if (isFeatureSupported(G2D_FAST_CLEAR)) {
        getTileStatus(handle, &surfaceX);
    } else {
        resolveTileStatus(handle);
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
            int c_stride = (alignWidth / 2 + 15) / 16 * 16;
            int stride = alignWidth;

            surface.stride = alignWidth;
            surface.planes[1] = surface.planes[0] + stride * alignHeight;
            surface.planes[2] = surface.planes[1] + c_stride * alignHeight / 2;
        } break;

        default:
            ALOGE("%s: does not support format:%d", __FUNCTION__, surface.format);
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

enum g2d_format DeviceComposer::convertFormat(int format, gralloc_handle_t handle) {
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
            ALOGE("%s: unsupported format:0x%x", __FUNCTION__, format);
            halFormat = G2D_RGBA8888;
            break;
    }

    halFormat = alterFormat(handle, halFormat);
    return halFormat;
}

int DeviceComposer::convertRotation(common::Transform transform, struct g2d_surface& src,
                                    struct g2d_surface& dst) {
    switch (transform) {
        case common::Transform::NONE:
            dst.rot = G2D_ROTATION_0;
            break;
        case common::Transform::ROT_90:
            dst.rot = G2D_ROTATION_90;
            break;
        case common::Transform::ROT_180:
            dst.rot = G2D_ROTATION_180;
            break;
        case common::Transform::ROT_270:
            dst.rot = G2D_ROTATION_270;
            break;
        case common::Transform::FLIP_H:
            dst.rot = G2D_FLIP_H;
            break;
        case common::Transform::FLIP_V:
            dst.rot = G2D_FLIP_V;
            break;
        case (common::Transform)(static_cast<int>(common::Transform::FLIP_H) |
                                 static_cast<int>(common::Transform::ROT_90)):
            dst.rot = G2D_ROTATION_90;
            src.rot = G2D_FLIP_H;
            break;
        case (common::Transform)(static_cast<int>(common::Transform::FLIP_V) |
                                 static_cast<int>(common::Transform::ROT_90)):
            dst.rot = G2D_ROTATION_90;
            src.rot = G2D_FLIP_V;
            break;
        default:
            dst.rot = G2D_ROTATION_0;
            break;
    }

    return 0;
}

int DeviceComposer::convertBlending(common::BlendMode blending, struct g2d_surface& src,
                                    struct g2d_surface& dst) {
    switch (blending) {
        case common::BlendMode::PREMULTIPLIED:
            src.blendfunc = G2D_ONE;
            dst.blendfunc = G2D_ONE_MINUS_SRC_ALPHA;
            break;

        case common::BlendMode::COVERAGE:
            src.blendfunc = G2D_SRC_ALPHA;
            dst.blendfunc = G2D_ONE_MINUS_SRC_ALPHA;
            break;

        default:
            src.blendfunc = G2D_ONE;
            dst.blendfunc = G2D_ONE_MINUS_SRC_ALPHA;
            break;
    }

    return 0;
}

int DeviceComposer::getAlignedSize(gralloc_handle_t handle, int* width, int* height) {
    if (mGetAlignedSize == NULL) {
        return -EINVAL;
    }

    return (*mGetAlignedSize)((void*)handle, (void*)width, (void*)height);
}

int DeviceComposer::getFlipOffset(gralloc_handle_t handle, int* offset) {
    if (mGetFlipOffset == NULL) {
        return -EINVAL;
    }

    return (*mGetFlipOffset)((void*)handle, (void*)offset);
}

int DeviceComposer::getTiling(gralloc_handle_t handle, enum g2d_tiling* tile) {
    if (mGetTiling == NULL) {
        return -EINVAL;
    }

    return (*mGetTiling)((void*)handle, (void*)tile);
}

enum g2d_format DeviceComposer::alterFormat(gralloc_handle_t handle, enum g2d_format format) {
    if (mAlterFormat == NULL) {
        return format;
    }

    return (enum g2d_format)(*mAlterFormat)((void*)handle, (void*)format);
}

int DeviceComposer::lockSurface(gralloc_handle_t handle) {
    if (mLockSurface == NULL) {
        return -EINVAL;
    }

    uint64_t phys = handle->phys;
    int ret = (*mLockSurface)((void*)handle);
    const_cast<gralloc_handle*>(handle)->phys = phys;

    return ret;
}

int DeviceComposer::unlockSurface(gralloc_handle_t handle) {
    if (mUnlockSurface == NULL) {
        return -EINVAL;
    }

    return (*mUnlockSurface)((void*)handle);
}

int DeviceComposer::setClipping(common::Rect& /*src*/, common::Rect& /*dst*/, common::Rect& clip,
                                common::Transform /*rotation*/) {
    if (mSetClipping == NULL) {
        return -EINVAL;
    }

    return (*mSetClipping)(getHandle(), (void*)(intptr_t)clip.left, (void*)(intptr_t)clip.top,
                           (void*)(intptr_t)clip.right, (void*)(intptr_t)clip.bottom);
}

int DeviceComposer::blitSurface(struct g2d_surfaceEx* srcEx, struct g2d_surfaceEx* dstEx) {
    if (mBlitFunction == NULL) {
        return -EINVAL;
    }

    return (*mBlitFunction)(getHandle(), srcEx, dstEx);
}

int DeviceComposer::openEngine(void** handle) {
    if (mOpenEngine == NULL) {
        return -EINVAL;
    }

    return (*mOpenEngine)((void*)handle);
}

int DeviceComposer::closeEngine(void* handle) {
    if (mCloseEngine == NULL) {
        return -EINVAL;
    }

    return (*mCloseEngine)((void*)handle);
}

int DeviceComposer::clearFunction(void* handle, struct g2d_surface* area) {
    if (mClearFunction == NULL) {
        return -EINVAL;
    }

    return (*mClearFunction)((void*)handle, area);
}

int DeviceComposer::enableFunction(void* handle, enum g2d_cap_mode cap, bool enable) {
    if (mEnableFunction == NULL || mDisableFunction == NULL) {
        return -EINVAL;
    }

    int ret = 0;
    if (enable) {
        ret = (*mEnableFunction)((void*)handle, (void*)cap);
    } else {
        ret = (*mDisableFunction)((void*)handle, (void*)cap);
    }

    return ret;
}

int DeviceComposer::finishEngine(void* handle) {
    if (mFinishEngine == NULL) {
        return -EINVAL;
    }

    return (*mFinishEngine)((void*)handle);
}

bool DeviceComposer::isFeatureSupported(g2d_feature feature) {
    if (mQueryFeature == NULL || getHandle() == NULL) {
        return false;
    }

    int enable = 0;
    (*mQueryFeature)(getHandle(), (void*)feature, (void*)&enable);
    return (enable != 0);
}

int DeviceComposer::alignTile(int* width, int* height, int format, int usage) {
    if (mAlignTile == NULL) {
        return -EINVAL;
    }
    return (*mAlignTile)(width, height, (void*)(intptr_t)format, (void*)(intptr_t)usage);
}

int DeviceComposer::getTileStatus(gralloc_handle_t handle, struct g2d_surfaceEx* surfaceX) {
    if (mGetTileStatus == NULL) {
        return -EINVAL;
    }

    return (*mGetTileStatus)((void*)handle, surfaceX);
}

int DeviceComposer::resolveTileStatus(gralloc_handle_t handle) {
    if (mResolveTileStatus == NULL) {
        return -EINVAL;
    }

    return (*mResolveTileStatus)((void*)handle);
}

bool DeviceComposer::checkMustDeviceComposition(Layer* layer) {
    DEBUG_LOG("%s: check layer %ld", __FUNCTION__, layer->getId());

    gralloc_handle_t layerBuffer = (gralloc_handle_t)(layer->getBuffer().getBuffer());
    // vpu tile format must be handled by device.
    if (layerBuffer != nullptr &&
        (layerBuffer->fslFormat == FORMAT_NV12_TILED || layerBuffer->usage & USAGE_PROTECTED)) {
        return true;
    }

    return false;
}

bool DeviceComposer::checkDeviceComposition(Layer* layer) {
    DEBUG_LOG("%s: check layer %ld", __FUNCTION__, layer->getId());

    if (!mG2dPrefered) {
        DEBUG_LOG("%s: 2d composition is not prefered, use 3D composition", __FUNCTION__);
        return false;
    }

    gralloc_handle_t layerBuffer = (gralloc_handle_t)(layer->getBuffer().getBuffer());
    common::Dataspace dataspace = layer->getDataspace();

    if (layer->getCompositionType() == Composition::CLIENT) {
        DEBUG_LOG("%s: Not process type=CLIENT layer", __FUNCTION__);
        return false;
    }

    if (layer->getColorTransform() != std::nullopt) {
        DEBUG_LOG("%s: g2d can't support color transform", __FUNCTION__);
        return false;
    }

    bool rotationCap = isFeatureSupported(G2D_ROTATION);
    // rotation case skip device composition.
    if ((layer->getTransform() != common::Transform::NONE) && !rotationCap) {
        DEBUG_LOG("%s: g2d can't support rotation", __FUNCTION__);
        return false;
    }

    // video nv12 full range should be handled by client
    if (layerBuffer != nullptr && layerBuffer->fslFormat == FORMAT_NV12 &&
        ((common::Dataspace)((int)dataspace & (int)common::Dataspace::RANGE_MASK) ==
         common::Dataspace::RANGE_FULL)) {
        DEBUG_LOG("%s: g2d can't support video nv12 full range", __FUNCTION__);
        return false;
    }

#ifdef WORKAROUND_DPU_ALPHA_BLENDING
    auto alpha = (uint8_t)(layer->getPlaneAlpha() * 255);
    // pixel alpha + blending + global alpha case skip device composition.
    if (layerBuffer != nullptr && alpha != 0xff &&
        layer->getBlendMode() == common::BlendMode::PREMULTIPLIED &&
        (layerBuffer->fslFormat == FORMAT_RGBA8888 || layerBuffer->fslFormat == FORMAT_BGRA8888 ||
         layerBuffer->fslFormat == FORMAT_RGBA1010102 ||
         layerBuffer->fslFormat == FORMAT_RGBAFP16)) {
        DEBUG_LOG("%s: format=%x, alpha=%x, blend=%x cannot process in DPU of imx8q", __func__,
                  layerBuffer->fslFormat, alpha, layer->getBlendMode());
        return false;
    }
#endif

    return true;
}

bool DeviceComposer::composeLayers(std::vector<Layer*> layers, buffer_handle_t target) {
    DEBUG_LOG("%s: %zu layers to target", __FUNCTION__, layers.size());

    if (!target) {
        ALOGE("%s: composer target buffer is invalid", __FUNCTION__);
        return false;
    }

    Mutex::Autolock _l(sLock);
    lockSurface((gralloc_handle_t)target);
    setRenderTarget((gralloc_handle_t)target);
    clearWormHole(layers);

    // to do composite.
    int i = 0, ret = 0;
    for (auto layer : layers) {
        if (layer->getCompositionType() == Composition::SIDEBAND)
            // set side band parameters.
            continue;

        gralloc_handle_t layerBuffer = (gralloc_handle_t)(layer->getBuffer().getBuffer());
        if (layerBuffer != NULL)
            lockSurface(layerBuffer);

        ret = composeLayerLocked(layer, i == 0);

        if (layerBuffer != NULL)
            unlockSurface(layerBuffer);

        if (ret != 0) {
            ALOGE("%s: compose layer %zu failed", __FUNCTION__, layer->getId());
            break;
        }
        i++;
    }

    unlockSurface((gralloc_handle_t)target);
    finishComposite();

    return 0;
}

} // namespace aidl::android::hardware::graphics::composer3::impl
