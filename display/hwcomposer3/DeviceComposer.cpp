/*
 * Copyright 2017-2024 NXP.
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
#include <drm_fourcc.h>
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

// Uncomment to enable additional debug logging for g2d only.
// #define DEBUG_NXP_HWC_G2D

#if defined(DEBUG_NXP_HWC_G2D)
#define DEBUG_LOG_G2D ALOGI
#else
#define DEBUG_LOG_G2D(...) ((void)0)
#endif

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

    if (!Is2DCompositionUserDisabled() && getDefaultG2DLib(g2dlibName, PATH_MAX)) {
        mG2dHandle = android_load_sphal_library(g2dlibName, RTLD_LOCAL | RTLD_NOW);
    }

    if (mG2dHandle == NULL) {
        ALOGI("can't find %s or user disabled, 2D composition is invalid", g2dlibName);
        mSetClipping = NULL;
        mBlitFunction = NULL;
        mOpenEngine = NULL;
        mCloseEngine = NULL;
        mClearFunction = NULL;
        mEnableFunction = NULL;
        mDisableFunction = NULL;
        mFinishEngine = NULL;
        mQueryFeature = NULL;
        mBuffInfoFromFd = NULL;
        mCreateFenceFd = NULL;
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
        mBuffInfoFromFd = (hwc_buf_func)dlsym(mG2dHandle, "g2d_buf_from_fd");
        mCreateFenceFd = (hwc_func1)dlsym(mG2dHandle, "g2d_create_fence_fd");
    }

    memset(&mSolidColorBuffInfo, 0, sizeof(mSolidColorBuffInfo));
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

int DeviceComposer::prepareDeviceFrameBuffer(uint32_t width, uint32_t height, uint32_t format,
                                             std::vector<buffer_handle_t>& buffers, int count,
                                             bool secure) {
    uint64_t usage;
    uint32_t bufferStride;
    buffer_handle_t bufferHandle;

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

        buffers.push_back(bufferHandle);
    }

    return 0;
}

int DeviceComposer::freeDeviceFrameBuffer(std::vector<buffer_handle_t>& buffers) {
    for (auto buf : buffers) {
        ::android::GraphicBufferAllocator::get().free(buf);
    }

    return 0;
}

int DeviceComposer::prepareSolidColorBuffer() {
    HandleInfo info;
    if (mTarget == NULL || (getInfoFromHandle(mTarget, &info) != 0)) {
        return 0;
    }

    if ((mSolidColorBuffer != NULL) &&
        (info.width == mSolidColorBuffInfo.width &&
         info.height == mSolidColorBuffInfo.height &&
         info.format == mSolidColorBuffInfo.format)) {
        return 0;
    }

    if (mSolidColorBuffer != NULL) {
        unlockSurface(mSolidColorBuffer);
        ::android::GraphicBufferAllocator::get().free(mSolidColorBuffer);
    }

    uint32_t bufferStride;
    buffer_handle_t bufferHandle;
    uint64_t usage = GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_2D |
            GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN;
    if (info.usage & GRALLOC_USAGE_PROTECTED)
        usage |= GRALLOC_USAGE_PROTECTED;

    auto status =
            ::android::GraphicBufferAllocator::get().allocate(info.width, info.height,
                                                              info.format, /*layerCount=*/1,
                                                              usage, &bufferHandle, &bufferStride,
                                                              "HwcSolidColor");
    if (status != ::android::OK) {
        ALOGE("%s: failed to allocate solid color buffer", __FUNCTION__);
        return -1;
    }

    mSolidColorBuffer = bufferHandle;
    if (getInfoFromHandle(mSolidColorBuffer, &mSolidColorBuffInfo) != 0) {
        ALOGE("%s: failed to get buffer info of solidcolor buffer", __FUNCTION__);
        return -1;
    }

    common::Rect rect;
    rect.left = rect.top = 0;
    rect.right = info.width;
    rect.bottom = info.height;
    lockSurface(mSolidColorBuffer);
    clearRect(mSolidColorBuffer, rect);

    return 0;
}

int DeviceComposer::freeSolidColorBuffer() {
    if (mSolidColorBuffer != NULL) {
        unlockSurface(mSolidColorBuffer);
        ::android::GraphicBufferAllocator::get().free(mSolidColorBuffer);
        mSolidColorBuffer = NULL;
    }

    return 0;
}

int DeviceComposer::finishComposite() {
    finishEngine(getHandle());
    return 0;
}

int DeviceComposer::setRenderTarget(buffer_handle_t memory) {
    mTarget = memory;
    HandleInfo info;
    if (mTarget == NULL || (getInfoFromHandle(mTarget, &info) != 0)) {
        return -EINVAL;
    }
    DEBUG_LOG_G2D("%s: --------target(fd=%d, %d x %d)--------", __FUNCTION__, info.fd, info.width,
                  info.height);
    return 0;
}

int DeviceComposer::clearRect(buffer_handle_t target, common::Rect& rect) {
    if (target == NULL || isRectEmpty(rect)) {
        return 0;
    }

    struct g2d_surfaceEx surfaceX;
    struct g2d_surface& surface = surfaceX.base;

    memset(&surfaceX, 0, sizeof(surfaceX));
    setG2dSurface(surfaceX, target, rect);
    surface.clrcolor = 0xff << 24;
    clearFunction(getHandle(), &surface);

    DEBUG_LOG_G2D("clearRect: rect(l:%d,t:%d,r:%d,b:%d)", rect.left, rect.top, rect.right,
                  rect.bottom);
    return 0;
}

int DeviceComposer::clearWormHole(std::vector<Layer*>& layers) {
    DEBUG_LOG("%s: clear worm hole", __FUNCTION__);
    HandleInfo info;
    if (mTarget == NULL || (getInfoFromHandle(mTarget, &info) != 0)) {
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
    ::android::Region screen(::android::Rect(info.width, info.height));
    screen.subtractSelf(opaque);
    const ::android::Rect* holes = NULL;
    size_t numRect = 0;
    holes = screen.getArray(&numRect);
    // clear worm hole.
    struct g2d_surfaceEx surfaceX;
    memset(&surfaceX, 0, sizeof(surfaceX));
    struct g2d_surface& surface = surfaceX.base;
    DEBUG_LOG_G2D("%s: clear %zu worm holes", __FUNCTION__, numRect);
    int clrcolor = 0x00 << 24; // make alpha be 0(transparent) for DRM_FORMAT_ABGR8888 like format.
    for (size_t i = 0; i < numRect; i++) {
        if (holes[i].isEmpty()) {
            continue;
        }

        common::Rect rect;
        rect.left = holes[i].left;
        rect.top = holes[i].top;
        rect.right = holes[i].right;
        rect.bottom = holes[i].bottom;
        DEBUG_LOG_G2D("clearhole: hole(l:%d,t:%d,r:%d,b:%d)", rect.left, rect.top, rect.right,
                      rect.bottom);
        setG2dSurface(surfaceX, mTarget, rect);
        surface.clrcolor = clrcolor;
        clearFunction(getHandle(), &surface);
    }

    return 0;
}

int DeviceComposer::composeLayerLocked(Layer* layer, bool bypass) {
    DEBUG_LOG("%s: compose layer %ld", __FUNCTION__, layer->getId());
    HandleInfo info;
    if (layer == NULL || mTarget == NULL || (getInfoFromHandle(mTarget, &info) != 0)) {
        ALOGE("%s: invalid layer or target", __FUNCTION__);
        return -EINVAL;
    }

    auto type = layer->getCompositionType();
    auto mode = layer->getBlendMode();
    auto transform = layer->getTransform();
    auto alpha = (uint8_t)(layer->getPlaneAlpha() * 255);
    auto layerBuffer = layer->getBuffer().getBuffer();

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
            DEBUG_LOG_G2D("%s: invalid clip", __FUNCTION__);
            continue;
        }

        if (!rectIntersect(drect, clip)) {
            DEBUG_LOG_G2D("%s: invalid clip rect", __FUNCTION__);
            continue;
        }

        setClipping(srect, drect, clip, transform);
        DEBUG_LOG_G2D("layer:%ld, sourceCrop(l:%d,t:%d,r:%d,b:%d), visible(l:%d,t:%d,r:%d,b:%d), "
                      "display(l:%d,t:%d,r:%d,b:%d)",
                      layer->getId(), srect.left, srect.top, srect.right, srect.bottom, clip.left,
                      clip.top, clip.right, clip.bottom, drect.left, drect.top, drect.right,
                      drect.bottom);

        HandleInfo layerInfo;
        if (layerBuffer != nullptr && (getInfoFromHandle(layerBuffer, &layerInfo) == 0)) {
            DEBUG_LOG_G2D("zorder:0x%x, phys:0x%" PRIx64, layer->getZOrder(), layerInfo.phys);
        }

        DEBUG_LOG_G2D("transform:0x%x, blend:0x%x, alpha:0x%x", transform, mode, alpha);

        setG2dSurface(dSurfaceX, mTarget, drect);

        struct g2d_surfaceEx sSurfaceX;
        memset(&sSurfaceX, 0, sizeof(sSurfaceX));
        struct g2d_surface& sSurface = sSurfaceX.base;

        if (!(type == Composition::SOLID_COLOR) && layerBuffer) {
            setG2dSurface(sSurfaceX, layerBuffer, srect);
            if ((info.format == static_cast<uint32_t>(common::PixelFormat::RGB_565)) &&
                (layerInfo.format == static_cast<uint32_t>(common::PixelFormat::RGBA_8888) ||
                 layerInfo.format == static_cast<uint32_t>(common::PixelFormat::RGBX_8888) ||
                 layerInfo.format == static_cast<uint32_t>(common::PixelFormat::BGRA_8888))) {
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

int DeviceComposer::setG2dSurface(struct g2d_surfaceEx& surfaceX, buffer_handle_t handle,
                                  common::Rect& rect) {
    int alignWidth = 0, alignHeight = 0;
    struct g2d_surface& surface = surfaceX.base;
    HandleInfo info;
    if (handle == NULL || (getInfoFromHandle(handle, &info) != 0)) {
        ALOGE("%s: handle is invalid!", __FUNCTION__);
        return -1;
    }

    int ret = getAlignedSize(handle, NULL, &alignHeight);
    if (ret != 0) {
        alignHeight = info.height;
    }

    alignWidth = info.stride;
    surface.format = convertFormat(info.drm_format, handle);
    surface.stride = alignWidth;
    enum g2d_tiling tile = G2D_LINEAR;
    getTiling(handle, &tile);
    if (info.modifier == DRM_FORMAT_MOD_AMPHION_TILED) {
        surfaceX.tiling = G2D_AMPHION_TILED;
    } else {
        surfaceX.tiling = tile;
    }

    if (isFeatureSupported(G2D_FAST_CLEAR)) {
        getTileStatus(handle, &surfaceX);
    } else {
        resolveTileStatus(handle);
    }

    int phys = 0;
    int offset = 0;
    if (info.phys)
        phys = info.phys;
    else
        getBuffPhys(handle, &phys);

    getFlipOffset(handle, &offset);
    surface.planes[0] = phys + offset;

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
            surface.planes[1] = surface.planes[0] + info.offsets[1];
            break;

        case G2D_I420:
        case G2D_YV12: {
            surface.stride = info.strides[0];
            surface.planes[1] = surface.planes[0] + info.offsets[1];
            surface.planes[2] = surface.planes[0] + info.offsets[2];
        } break;

        default:
            ALOGE("%s: does not support format:%d", __FUNCTION__, surface.format);
            break;
    }
    surface.left = rect.left;
    surface.top = rect.top;
    surface.right = rect.right;
    surface.bottom = rect.bottom;
    surface.width = info.width;
    surface.height = info.height;

    DEBUG_LOG_G2D("%s: dimension(%d,%d,%d,%d, %d x %d), format=%d, stride=%d, tiling=%d, "
                  "plane0=0x%x, plane1=0x%x, plane2=0x%x",
                  __FUNCTION__, surface.left, surface.top, surface.right, surface.bottom,
                  surface.width, surface.height, surface.format, surface.stride, surfaceX.tiling,
                  surface.planes[0], surface.planes[1], surface.planes[2]);

    return 0;
}

enum g2d_format DeviceComposer::convertFormat(int format, buffer_handle_t handle) {
    enum g2d_format halFormat;
    switch (format) {
        case DRM_FORMAT_ABGR2101010:
            halFormat = G2D_RGBA1010102;
            break;
        case DRM_FORMAT_ABGR8888:
            halFormat = G2D_RGBA8888;
            break;
        case DRM_FORMAT_XBGR8888:
            halFormat = G2D_RGBX8888;
            break;
        case DRM_FORMAT_RGB565:
            halFormat = G2D_RGB565;
            break;
        case DRM_FORMAT_ARGB8888:
            halFormat = G2D_BGRA8888;
            break;
        case DRM_FORMAT_NV21:
            halFormat = G2D_NV21;
            break;
        case DRM_FORMAT_NV12:
            halFormat = G2D_NV12;
            break;
        case DRM_FORMAT_YUV420:
            halFormat = G2D_I420;
            break;
        case DRM_FORMAT_YVU420_ANDROID:
        case DRM_FORMAT_YVU420:
            halFormat = G2D_YV12;
            break;
        case DRM_FORMAT_NV16:
            halFormat = G2D_NV16;
            break;
        case DRM_FORMAT_YUYV:
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

int DeviceComposer::getAlignedSize(buffer_handle_t handle, int* width, int* height) {
    if (mGetAlignedSize == NULL) {
        return -EINVAL;
    }

    return (*mGetAlignedSize)((void*)handle, (void*)width, (void*)height);
}

int DeviceComposer::getFlipOffset(buffer_handle_t handle, int* offset) {
    if (mGetFlipOffset == NULL) {
        return -EINVAL;
    }

    return (*mGetFlipOffset)((void*)handle, (void*)offset);
}

int DeviceComposer::getTiling(buffer_handle_t handle, enum g2d_tiling* tile) {
    if (mGetTiling == NULL) {
        return -EINVAL;
    }

    return (*mGetTiling)((void*)handle, (void*)tile);
}

enum g2d_format DeviceComposer::alterFormat(buffer_handle_t handle, enum g2d_format format) {
    if (mAlterFormat == NULL) {
        return format;
    }

    return (enum g2d_format)(*mAlterFormat)((void*)handle, (void*)format);
}

int DeviceComposer::lockSurface(buffer_handle_t handle) {
    if (mLockSurface == NULL) {
        return -EINVAL;
    }

    return (*mLockSurface)((void*)handle);
}

int DeviceComposer::unlockSurface(buffer_handle_t handle) {
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

int DeviceComposer::getBuffPhys(buffer_handle_t handle, int *phys) {
    if (mBuffInfoFromFd == NULL) {
        return -EINVAL;
    }

    HandleInfo info;
    if (handle == NULL || (getInfoFromHandle(handle, &info) != 0)) {
        ALOGE("%s: handle is invalid!", __FUNCTION__);
        return -EINVAL;
    }

    struct g2d_buf* buf = (struct g2d_buf*)(*mBuffInfoFromFd)((void*)(intptr_t)info.fd);
    if (buf && buf->buf_paddr)
        *phys = buf->buf_paddr;

    if (buf) {
        free(buf->buf_handle);
        free(buf);
    }

    return 0;
}

int DeviceComposer::createFenceFd(void* handle) {
    if (mCreateFenceFd == NULL) {
        return -1;
    }

    return (*mCreateFenceFd)((void*)handle);
}

int DeviceComposer::alignTile(int* width, int* height, int format, int usage) {
    if (mAlignTile == NULL) {
        return -EINVAL;
    }
    return (*mAlignTile)(width, height, (void*)(intptr_t)format, (void*)(intptr_t)usage);
}

int DeviceComposer::getTileStatus(buffer_handle_t handle, struct g2d_surfaceEx* surfaceX) {
    if (mGetTileStatus == NULL) {
        return -EINVAL;
    }

    return (*mGetTileStatus)((void*)handle, surfaceX);
}

int DeviceComposer::resolveTileStatus(buffer_handle_t handle) {
    if (mResolveTileStatus == NULL) {
        return -EINVAL;
    }

    return (*mResolveTileStatus)((void*)handle);
}

bool DeviceComposer::checkMustDeviceComposition(Layer* layer) {
    DEBUG_LOG("%s: check layer %ld", __FUNCTION__, layer->getId());

    auto layerBuffer = layer->getBuffer().getBuffer();
    HandleInfo info;
    if (layerBuffer == NULL || (getInfoFromHandle(layerBuffer, &info) != 0)) {
        return false;
    }

    // vpu tile format must be handled by device.
    if (layerBuffer != nullptr &&
        (info.modifier == DRM_FORMAT_MOD_AMPHION_TILED || info.usage & GRALLOC_USAGE_PROTECTED)) {
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

    auto layerBuffer = layer->getBuffer().getBuffer();
    HandleInfo info;
    if (layerBuffer == NULL || (getInfoFromHandle(layerBuffer, &info) != 0)) {
        return false;
    }

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

#ifdef G2D_LIMITATION_VIV
    if (info.drm_format == DRM_FORMAT_ABGR2101010) {
        DEBUG_LOG("%s: g2d can't support ABGR2101010 format", __FUNCTION__);
        return false;
    }

    common::Dataspace dataspace = layer->getDataspace();
    // video nv12 full range should be handled by client
    if (layerBuffer != nullptr && info.drm_format == DRM_FORMAT_NV12 &&
        ((common::Dataspace)((int)dataspace & (int)common::Dataspace::RANGE_MASK) ==
         common::Dataspace::RANGE_FULL)) {
        DEBUG_LOG("%s: g2d can't support video nv12 full range", __FUNCTION__);
        return false;
    }
#endif

    if (!(info.usage &
          (GRALLOC_USAGE_PROTECTED | GRALLOC_USAGE_PRIVATE_3 | GRALLOC_USAGE_HW_COMPOSER |
           GRALLOC_USAGE_HW_FB))) {
        ALOGI("%s: g2d can't support the buffer from system/system-uncached heap", __FUNCTION__);
        return false;
    }

    return true;
}

std::tuple<bool, ::android::base::unique_fd> DeviceComposer::composeLayers(
        std::vector<Layer*> layers, buffer_handle_t target) {
    DEBUG_LOG("%s: ------%zu layers compose to target-------", __FUNCTION__, layers.size());
    ATRACE_CALL();

    if (!target) {
        ALOGE("%s: composer target buffer is invalid", __FUNCTION__);
        return std::make_tuple(false, ::android::base::unique_fd());
    }

    Mutex::Autolock _l(sLock);
    lockSurface(target);
    setRenderTarget(target);
    clearWormHole(layers);

    // to do composite.
    int i = 0, ret = 0;
    for (auto layer : layers) {
        if (layer->getCompositionType() == Composition::SIDEBAND)
            // set side band parameters.
            continue;

        auto layerBuffer = layer->getBuffer().getBuffer();
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
    ::android::base::unique_fd composeFence(createFenceFd(getHandle()));

    unlockSurface(target);

    if (!composeFence.ok())
        finishComposite();

    return std::make_tuple(true, std::move(composeFence));
}

} // namespace aidl::android::hardware::graphics::composer3::impl
