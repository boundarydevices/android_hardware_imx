/*
 * Copyright (C) 2009-2013 Freescale Semiconductor, Inc. All Rights Reserved.
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


#include <hardware/hardware.h>

#include <fcntl.h>
#include <errno.h>

#define HWC_REMOVE_DEPRECATED_VERSIONS 1
#include <cutils/log.h>
#include <cutils/atomic.h>
#include <cutils/properties.h>
#include <utils/threads.h>
#include <hardware/hwcomposer.h>
#include <hardware_legacy/uevent.h>
#include <utils/StrongPointer.h>

#include <linux/mxcfb.h>
#include <linux/ioctl.h>
#include <EGL/egl.h>
#include "gralloc_priv.h"
#include "hwc_context.h"
#include "hwc_vsync.h"
#include "hwc_uevent.h"
#include "hwc_display.h"
#include <g2d.h>
#include <system/graphics.h>

static bool validateRect(hwc_rect_t& rect)
{
    if ((rect.left < 0) || (rect.top < 0) || (rect.right < 0) ||
        (rect.bottom < 0) || (rect.right - rect.left <= 0) ||
        (rect.bottom - rect.top <= 0)) {
        return false;
    }
    return true;
}

static enum g2d_format convertFormat(int format)
{
    switch (format) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
            return G2D_RGBA8888;
        case HAL_PIXEL_FORMAT_RGBX_8888:
            return G2D_RGBX8888;
        case HAL_PIXEL_FORMAT_RGB_565:
            return G2D_RGB565;
        case HAL_PIXEL_FORMAT_BGRA_8888:
            return G2D_BGRA8888;

        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
            return G2D_NV21;
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            return G2D_NV12;

        case HAL_PIXEL_FORMAT_YCbCr_420_P:
            return G2D_I420;
        case HAL_PIXEL_FORMAT_YV12:
            return G2D_YV12;

        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
            return G2D_NV16;
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            return G2D_YUYV;

        default:
            ALOGE("unsupported format:0x%x", format);
            return G2D_RGBA8888;
    }
}

static enum g2d_rotation convertRotation(int transform)
{
    switch (transform) {
        case 0:
            return G2D_ROTATION_0;
        case HAL_TRANSFORM_ROT_90:
            return G2D_ROTATION_90;
        case HAL_TRANSFORM_FLIP_H | HAL_TRANSFORM_FLIP_V:
            return G2D_ROTATION_180;
        case HAL_TRANSFORM_FLIP_H | HAL_TRANSFORM_FLIP_V |
             HAL_TRANSFORM_ROT_90:
            return G2D_ROTATION_270;
        case HAL_TRANSFORM_FLIP_H:
            return G2D_FLIP_H;
        case HAL_TRANSFORM_FLIP_V:
            return G2D_FLIP_V;
        default:
            return G2D_ROTATION_0;
    }
}

static int convertBlending(int blending, struct g2d_surface& src,
                        struct g2d_surface& dst)
{
    switch (blending) {
        case HWC_BLENDING_PREMULT:
            src.blendfunc = G2D_ONE;
            dst.blendfunc = G2D_ONE_MINUS_SRC_ALPHA;
            break;

        case HWC_BLENDING_COVERAGE:
            src.blendfunc = G2D_SRC_ALPHA;
            dst.blendfunc = G2D_ONE_MINUS_SRC_ALPHA;
            break;

        case HWC_BLENDING_DIM:
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

static int setG2dSurface(struct g2d_surface& surface,
             struct private_handle_t *handle, hwc_rect_t& rect)
{
    surface.format = convertFormat(handle->format);
    surface.planes[0] = handle->phys;
    surface.stride = handle->flags >> 16;
    switch (surface.format) {
        case G2D_RGB565: {
            int alignHeight = (handle->height + 3) & ~0x3;
            surface.planes[0] += surface.stride * 2 * (alignHeight - handle->height);
            } break;
        case G2D_RGBA8888:
        case G2D_BGRA8888:
        case G2D_RGBX8888: {
            int alignHeight = (handle->height + 3) & ~0x3;
            surface.planes[0] += surface.stride * 4 * (alignHeight - handle->height);
            } break;

        case G2D_YUYV:
            break;

        case G2D_NV16:
        case G2D_NV12:
        case G2D_NV21: {
            int stride = surface.stride;
            surface.planes[1] = handle->phys + stride * handle->height;
            } break;

        case G2D_I420:
        case G2D_YV12: {
            int stride = surface.stride;
            int c_stride = (stride/2+15)/16*16;
            surface.planes[1] = handle->phys + stride * handle->height;
            surface.planes[2] = surface.planes[1] + c_stride * handle->height;
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

static inline int min(int a, int b) {
    return (a<b) ? a : b;
}

static inline int max(int a, int b) {
    return (a>b) ? a : b;
}

void convertScalerToInt(hwc_frect_t& in, hwc_rect_t& out)
{
    out.left = (int)in.left;
    out.top = (int)in.top;
    out.right = (int)in.right;
    out.bottom = (int)in.bottom;
}

static void intersect(hwc_rect_t* out, const hwc_rect_t* lhs,
            const hwc_rect_t* rhs)
{
    if (out == NULL || lhs == NULL || rhs == NULL) {
        return;
    }

    out->left = max(lhs->left, rhs->left);
    out->top = max(lhs->top, rhs->top);
    out->right = min(lhs->right, rhs->right);
    out->bottom = min(lhs->bottom, rhs->bottom);
}

static void setRects(hwc_rect_t& src, hwc_rect_t& dst,
                     const hwc_rect_t& dstClip, int rotation)
{
    hwc_rect_t drect = dst;
    hwc_rect_t srect = src;
    int32_t srcW, srcH, dstW, dstH, deltaX, deltaY;
    intersect(&dst, &dstClip, &drect);

    //rotate 90
    if (rotation == HAL_TRANSFORM_ROT_90) {
        dstW = drect.bottom - drect.top;
        dstH = drect.right - drect.left;
        deltaX = dst.top - drect.top;
        deltaY = -dst.right + drect.right;
    }
    //virtical flip
    else if (rotation == HAL_TRANSFORM_FLIP_V) {
        dstW = drect.right - drect.left;
        dstH = drect.bottom - drect.top;
        deltaX = dst.left - drect.left;
        deltaY = -dst.bottom + drect.bottom;
    }
    //horizontal flip
    else if (rotation == HAL_TRANSFORM_FLIP_H) {
        dstW = drect.right - drect.left;
        dstH = drect.bottom - drect.top;
        deltaX = -dst.right + drect.right;
        deltaY = dst.top - drect.top;
    }
    //rotate 270
    else if (rotation == (HAL_TRANSFORM_FLIP_H | HAL_TRANSFORM_FLIP_V |
             HAL_TRANSFORM_ROT_90)) {
        dstW = drect.bottom - drect.top;
        dstH = drect.right - drect.left;
        deltaY = dst.left - drect.left;
        deltaX = -dst.bottom + drect.bottom;
    }
    //rotate 180
    else if (rotation == (HAL_TRANSFORM_FLIP_H | HAL_TRANSFORM_FLIP_V)) {
        dstW = drect.right - drect.left;
        dstH = drect.bottom - drect.top;
        deltaY = -dst.bottom + drect.bottom;
        deltaX = -dst.right + drect.right;
    }
    //rotate 0
    else {
        dstW = drect.right - drect.left;
        dstH = drect.bottom - drect.top;
        deltaX = dst.left - drect.left;
        deltaY = dst.top - drect.top;
    }
    srcW = srect.right - srect.left;
    srcH = srect.bottom - srect.top;

    deltaX = deltaX * srcW / dstW;
    deltaY = deltaY * srcH / dstH;

    src.left = deltaX + srect.left;
    src.top = deltaY + srect.top;
    if (rotation & HAL_TRANSFORM_ROT_90) {
        src.right = src.left + (dstClip.bottom - dstClip.top)* srcW / dstW;
        src.bottom = src.top + (dstClip.right - dstClip.left)* srcH / dstH;
    }
    else {
        src.right = src.left + (dstClip.right - dstClip.left) * srcW / dstW;
        src.bottom = src.top + (dstClip.bottom - dstClip.top) * srcH / dstH;
    }
}

int hwc_composite(struct hwc_context_t* ctx, hwc_layer_1_t* layer,
                 struct private_handle_t *dstHandle, bool firstLayer)
{
    if (ctx == NULL || ctx->g2d_handle == NULL || layer == NULL || dstHandle == NULL) {
        ALOGE("%s: invalid parameters", __FUNCTION__);
        return -EINVAL;
    }

    //sourceCrop change with version.
    hwc_rect_t srect;
    convertScalerToInt(layer->sourceCropf, srect);
    if (!validateRect(srect) && layer->blending != HWC_BLENDING_DIM) {
        ALOGE("%s: invalid sourceCrop(l:%d,t:%d,r:%d,b:%d)", __FUNCTION__,
                 srect.left, srect.top, srect.right, srect.bottom);
        return -EINVAL;
    }

    hwc_rect_t drect = layer->displayFrame;
    if (!validateRect(drect)) {
        ALOGE("%s: invalid displayFrame(l:%d,t:%d,r:%d,b:%d)", __FUNCTION__,
                 drect.left, drect.top, drect.right, drect.bottom);
        return -EINVAL;
    }

    struct g2d_surface dSurface;
    memset(&dSurface, 0, sizeof(dSurface));

    for (size_t i=0; i<layer->visibleRegionScreen.numRects; i++) {
        convertScalerToInt(layer->sourceCropf, srect);
        drect = layer->displayFrame;
        if (!validateRect((hwc_rect_t&)layer->visibleRegionScreen.rects[i])) {
            ALOGI("invalid clip rect");
            continue;
        }
        setRects(srect, drect, layer->visibleRegionScreen.rects[i],
                  layer->transform);
        if (!validateRect(srect) && layer->blending != HWC_BLENDING_DIM) {
            ALOGI("%s: invalid srect(l:%d,t:%d,r:%d,b:%d)", __FUNCTION__,
                    srect.left, srect.top, srect.right, srect.bottom);
            hwc_rect_t src;
            convertScalerToInt(layer->sourceCropf, src);
            hwc_rect_t& vis = (hwc_rect_t&)layer->visibleRegionScreen.rects[i];
            hwc_rect_t& dis = layer->displayFrame;
            ALOGI("sourceCrop(l:%d,t:%d,r:%d,b:%d), visible(l:%d,t:%d,r:%d,b:%d), "
                    "display(l:%d,t:%d,r:%d,b:%d)",
                    src.left, src.top, src.right, src.bottom,
                    vis.left, vis.top, vis.right, vis.bottom,
                    dis.left, dis.top, dis.right, dis.bottom);
            continue;
        }
        if (!validateRect(drect)) {
            ALOGI("%s: invalid drect(l:%d,t:%d,r:%d,b:%d)", __FUNCTION__,
                    drect.left, drect.top, drect.right, drect.bottom);
            continue;
        }

        ALOGV("draw: src(l:%d,t:%d,r:%d,b:%d), dst(l:%d,t:%d,r:%d,b:%d)",
                srect.left, srect.top, srect.right, srect.bottom,
                drect.left, drect.top, drect.right, drect.bottom);

        setG2dSurface(dSurface, dstHandle, drect);
        dSurface.rot = convertRotation(layer->transform);

        struct g2d_surface sSurface;
        memset(&sSurface, 0, sizeof(sSurface));

        if (layer->blending != HWC_BLENDING_DIM) {
            struct private_handle_t *priv_handle;
            priv_handle = (struct private_handle_t *)(layer->handle);
            setG2dSurface(sSurface, priv_handle, srect);
        }
        else {
            sSurface.clrcolor = 0xff000000;
            sSurface.format = G2D_RGBA8888;
        }
        ALOGV("blit rot:%d, blending:%d, alpha:%d", layer->transform,
                layer->blending, layer->planeAlpha);

        if (!firstLayer) {
            convertBlending(layer->blending, sSurface, dSurface);
        }
        sSurface.global_alpha = layer->planeAlpha;

        if (layer->blending != HWC_BLENDING_NONE && !firstLayer) {
            g2d_enable(ctx->g2d_handle, G2D_GLOBAL_ALPHA);
            if (layer->blending == HWC_BLENDING_DIM) {
                ALOGV("enable blend dim");
                g2d_enable(ctx->g2d_handle, G2D_BLEND_DIM);
            }
            else {
                g2d_enable(ctx->g2d_handle, G2D_BLEND);
            }
        }

        g2d_blit(ctx->g2d_handle, &sSurface, &dSurface);

        if (layer->blending != HWC_BLENDING_NONE && !firstLayer) {
            if (layer->blending == HWC_BLENDING_DIM) {
                g2d_disable(ctx->g2d_handle, G2D_BLEND_DIM);
            }
            else {
                g2d_disable(ctx->g2d_handle, G2D_BLEND);
            }
            g2d_disable(ctx->g2d_handle, G2D_GLOBAL_ALPHA);
        }
    }

    return 0;
}

int hwc_clearWormHole(struct hwc_context_t* ctx, struct private_handle_t *dstHandle,
                    hwc_region_t &hole)
{
    if (ctx == NULL || dstHandle == NULL || hole.rects == NULL) {
        return -EINVAL;
    }

    struct g2d_surface surface;
    memset(&surface, 0, sizeof(surface));
    for (size_t i=0; i<hole.numRects; i++) {
        hwc_rect_t& rect = (hwc_rect_t&)hole.rects[i];
        if (!validateRect(rect)) {
            continue;
        }

        setG2dSurface(surface, dstHandle, rect);
        surface.clrcolor = 0xff << 24;
        g2d_clear(ctx->g2d_handle, &surface);
    }

    return 0;
}

int hwc_clearRect(struct hwc_context_t* ctx, struct private_handle_t *dstHandle,
                    hwc_rect_t &rect)
{
    if (ctx == NULL || dstHandle == NULL) {
        return -EINVAL;
    }

    if (!validateRect(rect)) {
        return -EINVAL;
    }

    struct g2d_surface surface;
    memset(&surface, 0, sizeof(surface));

    setG2dSurface(surface, dstHandle, rect);
    surface.clrcolor = 0xff << 24;
    g2d_clear(ctx->g2d_handle, &surface);

    return 0;
}

