/*
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Copyright 2023 NXP
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "helpers.h"

#include <MemoryDesc.h>
#include <assert.h>
#include <cutils/log.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <xf86drm.h>

using namespace fsl;

struct planar_layout {
    size_t num_planes;
    int horizontal_subsampling[DRV_MAX_PLANES];
    int vertical_subsampling[DRV_MAX_PLANES];
    int bytes_per_pixel[DRV_MAX_PLANES];
};

// clang-format off

static const struct planar_layout packed_1bpp_layout = {
	.num_planes = 1,
	.horizontal_subsampling = { 1 },
	.vertical_subsampling = { 1 },
	.bytes_per_pixel = { 1 }
};

static const struct planar_layout packed_2bpp_layout = {
	.num_planes = 1,
	.horizontal_subsampling = { 1 },
	.vertical_subsampling = { 1 },
	.bytes_per_pixel = { 2 }
};

static const struct planar_layout packed_3bpp_layout = {
	.num_planes = 1,
	.horizontal_subsampling = { 1 },
	.vertical_subsampling = { 1 },
	.bytes_per_pixel = { 3 }
};

static const struct planar_layout packed_4bpp_layout = {
	.num_planes = 1,
	.horizontal_subsampling = { 1 },
	.vertical_subsampling = { 1 },
	.bytes_per_pixel = { 4 }
};

static const struct planar_layout packed_8bpp_layout = {
	.num_planes = 1,
	.horizontal_subsampling = { 1 },
	.vertical_subsampling = { 1 },
	.bytes_per_pixel = { 8 }
};

static const struct planar_layout biplanar_yuv_420_layout = {
	.num_planes = 2,
	.horizontal_subsampling = { 1, 2 },
	.vertical_subsampling = { 1, 2 },
	.bytes_per_pixel = { 1, 2 }
};

static const struct planar_layout biplanar_yuv_422_layout = {
	.num_planes = 2,
	.horizontal_subsampling = { 1, 2 },
	.vertical_subsampling = { 1, 1 },
	.bytes_per_pixel = { 1, 2 }
};

static const struct planar_layout triplanar_yuv_420_layout = {
	.num_planes = 3,
	.horizontal_subsampling = { 1, 2, 2 },
	.vertical_subsampling = { 1, 2, 2 },
	.bytes_per_pixel = { 1, 1, 1 }
};

static const struct planar_layout biplanar_yuv_p010_layout = {
	.num_planes = 2,
	.horizontal_subsampling = { 1, 2 },
	.vertical_subsampling = { 1, 2 },
	.bytes_per_pixel = { 2, 4 }
};

// clang-format on

static const struct planar_layout *layout_from_format(uint32_t format) {
    switch (format) {
        case DRM_FORMAT_BGR233:
        case DRM_FORMAT_C8:
        case DRM_FORMAT_R8:
        case DRM_FORMAT_RGB332:
        case FORMAT_BLOB: // DRM_FORMAT_R8
            return &packed_1bpp_layout;

        case DRM_FORMAT_R16:
            return &packed_2bpp_layout;

        case DRM_FORMAT_YVU420:
        case DRM_FORMAT_YVU420_ANDROID:
        // case FORMAT_YV12://DRM_FORMAT_YVU420_ANDROID
        case FORMAT_I420:
            return &triplanar_yuv_420_layout;

        case DRM_FORMAT_NV12:
        case DRM_FORMAT_NV21:
        case DRM_FORMAT_FLEX_YCbCr_420_888:
        case FORMAT_NV21: // DRM_FORMAT_NV21    ????
        case FORMAT_NV12: // DRM_FORMAT_NV12    ????
        case FORMAT_NV12_TILED:
            return &biplanar_yuv_420_layout;

        case FORMAT_YCBCR_P010:
        case DRM_FORMAT_P010:
        case FORMAT_P010: // DRM_FORMAT_P010
        case FORMAT_P010_TILED:
            return &biplanar_yuv_p010_layout;

        case DRM_FORMAT_ABGR1555:
        case DRM_FORMAT_ABGR4444:
        case DRM_FORMAT_ARGB1555:
        case DRM_FORMAT_ARGB4444:
        case DRM_FORMAT_BGR565:
        case DRM_FORMAT_BGRA4444:
        case DRM_FORMAT_BGRA5551:
        case DRM_FORMAT_BGRX4444:
        case DRM_FORMAT_BGRX5551:
        case DRM_FORMAT_GR88:
        case DRM_FORMAT_RG88:
        case DRM_FORMAT_RGB565:
        case DRM_FORMAT_RGBA4444:
        case DRM_FORMAT_RGBA5551:
        case DRM_FORMAT_RGBX4444:
        case DRM_FORMAT_RGBX5551:
        case DRM_FORMAT_UYVY:
        case DRM_FORMAT_VYUY:
        case DRM_FORMAT_XBGR1555:
        case DRM_FORMAT_XBGR4444:
        case DRM_FORMAT_XRGB1555:
        case DRM_FORMAT_XRGB4444:
        case DRM_FORMAT_YUYV:
        case DRM_FORMAT_YVYU:
        case DRM_FORMAT_MTISP_SXYZW10:
        case FORMAT_RGB565: // DRM_FORMAT_RGB565
        case FORMAT_YUYV:   // DRM_FORMAT_YUYV   ????
        case FORMAT_RAW16:
            return &packed_2bpp_layout;

        case DRM_FORMAT_BGR888:
        case DRM_FORMAT_RGB888:
        case FORMAT_RGB888: // DRM_FORMAT_BGR888
            return &packed_3bpp_layout;

        case DRM_FORMAT_ABGR2101010:
        case DRM_FORMAT_ABGR8888:
        case DRM_FORMAT_ARGB2101010:
        case DRM_FORMAT_ARGB8888:
        case DRM_FORMAT_AYUV:
        case DRM_FORMAT_BGRA1010102:
        case DRM_FORMAT_BGRA8888:
        case DRM_FORMAT_BGRX1010102:
        case DRM_FORMAT_BGRX8888:
        case DRM_FORMAT_RGBA1010102:
        case DRM_FORMAT_RGBA8888:
        case DRM_FORMAT_RGBX1010102:
        case DRM_FORMAT_RGBX8888:
        case DRM_FORMAT_XBGR2101010:
        case DRM_FORMAT_XBGR8888:
        case DRM_FORMAT_XRGB2101010:
        case DRM_FORMAT_XRGB8888:
        case FORMAT_RGBA8888:    // DRM_FORMAT_ABGR8888
        case FORMAT_RGBX8888:    // DRM_FORMAT_XBGR8888
        case FORMAT_BGRA8888:    // DRM_FORMAT_ARGB8888
        case FORMAT_RGBA1010102: // DRM_FORMAT_ABGR2101010
            return &packed_4bpp_layout;

        case DRM_FORMAT_ABGR16161616F:
        case FORMAT_RGBAFP16: // DRM_FORMAT_ABGR16161616F
            return &packed_8bpp_layout;

        case FORMAT_NV16:
            return &biplanar_yuv_422_layout;

        case FORMAT_NV12_G1_TILED:
        case FORMAT_NV12_G2_TILED:
        case FORMAT_NV12_G2_TILED_COMPRESSED:
        case FORMAT_P010_TILED_COMPRESSED:
        default:
            ALOGE("%s UNKNOWN FORMAT 0x%x", __func__, format);
            return NULL;
    }
}

size_t drv_num_planes_from_format(uint32_t format) {
    const struct planar_layout *layout = layout_from_format(format);

    /*
     * drv_bo_new calls this function early to query number of planes and
     * considers 0 planes to mean unknown format, so we have to support
     * that.  All other layout_from_format() queries can assume that the
     * format is supported and that the return value is non-NULL.
     */

    return layout ? layout->num_planes : 0;
}

uint32_t drv_height_from_format(uint32_t format, uint32_t height, size_t plane) {
    const struct planar_layout *layout = layout_from_format(format);
    uint32_t alignedh;

    assert(plane < layout->num_planes);

    switch (format) {
        case FORMAT_NV12:
        case FORMAT_NV21:
        case FORMAT_I420:
        case FORMAT_YV12:
        case FORMAT_YUYV:
            alignedh = ALIGN_PIXEL_4(height);
            break;
        default:
            alignedh = height;
            break;
    }

    return DIV_ROUND_UP(alignedh, layout->vertical_subsampling[plane]);
}

uint32_t drv_vertical_subsampling_from_format(uint32_t format, size_t plane) {
    const struct planar_layout *layout = layout_from_format(format);

    assert(plane < layout->num_planes);

    return layout->vertical_subsampling[plane];
}

uint32_t drv_bytes_per_pixel_from_format(uint32_t format, size_t plane) {
    const struct planar_layout *layout = layout_from_format(format);

    assert(plane < layout->num_planes);

    return layout->bytes_per_pixel[plane];
}

/*
 * This function returns the stride for a given format, width and plane.
 */
uint32_t drv_stride_from_format(uint32_t format, uint32_t width, size_t plane) {
    if (format == FORMAT_NV12_TILED || format == FORMAT_NV12_G1_TILED ||
        format == FORMAT_NV12_G2_TILED || format == FORMAT_NV12_G2_TILED_COMPRESSED ||
        format == FORMAT_P010 || format == FORMAT_P010_TILED ||
        format == FORMAT_P010_TILED_COMPRESSED) {
        return width; // TODO: workaround for these formats that no layout
    }

    const struct planar_layout *layout = layout_from_format(format);
    assert(plane < layout->num_planes);

    uint32_t plane_width = DIV_ROUND_UP(width, layout->horizontal_subsampling[plane]);
    uint32_t stride = plane_width * layout->bytes_per_pixel[plane];

    /*
     * The stride of Android YV12 buffers is required to be aligned to 16 bytes
     * (see <system/graphics.h>).
     */
    if (format == DRM_FORMAT_YVU420_ANDROID)
        stride = (plane == 0) ? ALIGN(stride, 32) : ALIGN(stride, 16);

    return stride;
}

uint32_t drv_size_from_format(uint32_t format, uint32_t stride, uint32_t height, size_t plane) {
    return stride * drv_height_from_format(format, height, plane);
}

static uint32_t subsample_stride(uint32_t stride, uint32_t format, size_t plane) {
    if (plane != 0) {
        switch (format) {
            case DRM_FORMAT_YVU420:
            case DRM_FORMAT_YVU420_ANDROID:
                stride = DIV_ROUND_UP(stride, 2);
                break;
        }
    }

    return stride;
}

/*
 * This function fills in the buffer object given the driver aligned stride of
 * the first plane, height and a format. This function assumes there is just
 * one kernel buffer per buffer object.
 */
int drv_bo_from_format(gralloc_handle *hnd, uint32_t stride, uint32_t aligned_height,
                       uint32_t format) {
    uint32_t padding[DRV_MAX_PLANES] = {0};
    return drv_bo_from_format_and_padding(hnd, stride, aligned_height, format, padding);
}

int drv_bo_from_format_and_padding(gralloc_handle *hnd, uint32_t stride, uint32_t aligned_height,
                                   uint32_t format, uint32_t padding[DRV_MAX_PLANES]) {
    size_t p, num_planes;
    uint32_t offset = 0;

    num_planes = drv_num_planes_from_format(format);
    assert(num_planes);

    /*
     * HAL_PIXEL_FORMAT_YV12 requires that (see <system/graphics.h>):
     *  - the aligned height is same as the buffer's height.
     *  - the chroma stride is 16 bytes aligned, i.e., the luma's strides
     *    is 32 bytes aligned.
     */
    if (format == DRM_FORMAT_YVU420_ANDROID) {
        assert(aligned_height == hnd->height);
        assert(stride == ALIGN(stride, 32));
    }

    for (p = 0; p < num_planes; p++) {
        hnd->strides[p] = subsample_stride(stride, format, p);
        hnd->sizes[p] =
                drv_size_from_format(format, hnd->strides[p], aligned_height, p) + padding[p];
        hnd->offsets[p] = offset;
        offset += hnd->sizes[p];
    }

    hnd->total_size = offset;
    return 0;
}

/*
 * Map internal fourcc codes back to standard fourcc codes.
 */
uint32_t drv_get_standard_fourcc(uint32_t fourcc_internal) {
    return (fourcc_internal == DRM_FORMAT_YVU420_ANDROID) ? DRM_FORMAT_YVU420 : fourcc_internal;
}
