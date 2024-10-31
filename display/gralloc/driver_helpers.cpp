/*
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Copyright 2024 NXP
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver_helpers.h"

#include <aidl/android/hardware/graphics/common/BufferUsage.h>
#include <aidl/android/hardware/graphics/common/PixelFormat.h>
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

#include "driver_utils.h"

struct planar_layout {
    size_t num_planes;
    int horizontal_subsampling[DRV_MAX_PLANES];
    int vertical_subsampling[DRV_MAX_PLANES];
    int bits_per_pixel[DRV_MAX_PLANES];
};

// clang-format off

static const struct planar_layout packed_1bpp_layout = {
	.num_planes = 1,
	.horizontal_subsampling = { 1 },
	.vertical_subsampling = { 1 },
	.bits_per_pixel = { 8 }
};

static const struct planar_layout packed_2bpp_layout = {
	.num_planes = 1,
	.horizontal_subsampling = { 1 },
	.vertical_subsampling = { 1 },
	.bits_per_pixel = { 16 }
};

static const struct planar_layout packed_3bpp_layout = {
	.num_planes = 1,
	.horizontal_subsampling = { 1 },
	.vertical_subsampling = { 1 },
	.bits_per_pixel = { 24 }
};

static const struct planar_layout packed_4bpp_layout = {
	.num_planes = 1,
	.horizontal_subsampling = { 1 },
	.vertical_subsampling = { 1 },
	.bits_per_pixel = { 32 }
};

static const struct planar_layout packed_8bpp_layout = {
	.num_planes = 1,
	.horizontal_subsampling = { 1 },
	.vertical_subsampling = { 1 },
	.bits_per_pixel = { 64 }
};

static const struct planar_layout biplanar_yuv_420_layout = {
	.num_planes = 2,
	.horizontal_subsampling = { 1, 2 },
	.vertical_subsampling = { 1, 2 },
	.bits_per_pixel = { 8, 16 }
};

static const struct planar_layout biplanar_yuv_422_layout = {
	.num_planes = 2,
	.horizontal_subsampling = { 1, 2 },
	.vertical_subsampling = { 1, 1 },
	.bits_per_pixel = { 8, 16 }
};

static const struct planar_layout triplanar_yuv_420_layout = {
	.num_planes = 3,
	.horizontal_subsampling = { 1, 2, 2 },
	.vertical_subsampling = { 1, 2, 2 },
	.bits_per_pixel = { 8, 8, 8 }
};

static const struct planar_layout biplanar_yuv_p010_layout = {
	.num_planes = 2,
	.horizontal_subsampling = { 1, 2 },
	.vertical_subsampling = { 1, 2 },
	.bits_per_pixel = { 16, 32 }
};

static const struct planar_layout biplanar_yuv_nv15_layout = {
	.num_planes = 2,
	.horizontal_subsampling = { 1, 2 },
	.vertical_subsampling = { 1, 2 },
	.bits_per_pixel = { 10, 20 }
};

// clang-format on

static const struct planar_layout *layout_from_format(uint32_t format) {
    switch (format) {
        case DRM_FORMAT_BGR233:
        case DRM_FORMAT_C8:
        case DRM_FORMAT_R8:
        case DRM_FORMAT_RGB332:
            return &packed_1bpp_layout;

        case DRM_FORMAT_R16:
            return &packed_2bpp_layout;

        case DRM_FORMAT_YVU420:
            return &triplanar_yuv_420_layout;

        case DRM_FORMAT_NV12:
        case DRM_FORMAT_NV21:
            return &biplanar_yuv_420_layout;

        case DRM_FORMAT_P010:
            return &biplanar_yuv_p010_layout;

        case DRM_FORMAT_NV15:
            return &biplanar_yuv_nv15_layout;

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
            return &packed_2bpp_layout;

        case DRM_FORMAT_BGR888:
        case DRM_FORMAT_RGB888:
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
            return &packed_4bpp_layout;

        case DRM_FORMAT_ABGR16161616F:
            return &packed_8bpp_layout;

        case DRM_FORMAT_NV16:
            return &biplanar_yuv_422_layout;

        default:
            ALOGE("%s UNKNOWN FORMAT 0x%x", __func__, format);
            return NULL;
    }
}

bool drv_pixel_width_height_alignment(gralloc_buffer_descriptor *desc, uint32_t *aligned_width,
                                      uint32_t *aligned_height) {
    auto info = getPixleFormatInfo(desc->pixel_format);
    if (!info)
        return false;

    if (info->is_rgb) {
        if (desc->usage & USAGE_GPU_TILED_VIV) {
            if ((desc->flags & NXP_GRALLOC_FLAGS_FRAMEBUFFER) &&
                (desc->flags & NXP_GRALLOC_FLAGS_DISPLAY_UNDERRUN)) {
                *aligned_width = ALIGN_PIXEL_16(desc->width);
                *aligned_height = ALIGN_PIXEL_16(desc->height);
            } else {
                *aligned_width = ALIGN_PIXEL_64(desc->width);
                *aligned_height = ALIGN_PIXEL_64(desc->height);
            }
        } else {
            *aligned_width = ALIGN_PIXEL_16(desc->width);
            *aligned_height = ALIGN_PIXEL_16(desc->height);
        }
    } else if (desc->pixel_format == static_cast<int32_t>(PixelFormat::YV12) ||
               desc->pixel_format == HAL_PIXEL_FORMAT_YCbCr_420_P) {
        *aligned_width = ALIGN_PIXEL_32(desc->width);
        *aligned_height = ALIGN_PIXEL_4(desc->height);
    } else if (desc->pixel_format == HAL_PIXEL_FORMAT_NV12_TILED ||
               desc->pixel_format == HAL_PIXEL_FORMAT_P010_TILED) {
        *aligned_width = ALIGN_PIXEL_256(desc->width);
        *aligned_height = ALIGN_PIXEL_256(desc->height);
    } else if (info->is_yuv) {
        *aligned_width = ALIGN_PIXEL_16(desc->width);
        if (desc->usage & USAGE_PADDING_BUFFER)
            *aligned_height = ALIGN_PIXEL_16(desc->height);
        else
            *aligned_height = ALIGN_PIXEL_4(desc->height);
    } else {
        *aligned_width = desc->width;
        *aligned_height = desc->height;
    }

    /* Below is workaround for 10 bit format(NV15), It make sure the width/size are aligned with
     * codec2 side. This aligned_width should be stride in bytes, not pixle stride.
     */
    if (desc->pixel_format == HAL_PIXEL_FORMAT_P010 ||
        desc->pixel_format == HAL_PIXEL_FORMAT_P010_TILED_COMPRESSED) {
        *aligned_width = ALIGN(desc->width * 5 / 4, 16);
    } else if (desc->pixel_format == HAL_PIXEL_FORMAT_P010_TILED) {
        *aligned_width = ALIGN(desc->width * 5 / 4, 256);
    }

    return true;
}

size_t drv_num_planes_from_format(uint32_t format) {
    const struct planar_layout *layout = layout_from_format(format);

    return layout ? layout->num_planes : 0;
}

uint32_t drv_height_from_format(uint32_t fourcc, uint32_t height, size_t plane) {
    const struct planar_layout *layout = layout_from_format(fourcc);
    uint32_t alignedh;

    assert(plane < layout->num_planes);

    switch (fourcc) {
        case DRM_FORMAT_NV12:
        case DRM_FORMAT_NV21:
        case DRM_FORMAT_YUV420:
        case DRM_FORMAT_YVU420:
        case DRM_FORMAT_YUYV:
            alignedh = ALIGN_PIXEL_4(height);
            break;
        default:
            alignedh = height;
            break;
    }

    return DIV_ROUND_UP(alignedh, layout->vertical_subsampling[plane]);
}

/*
 * This function returns the stride in bytes for a given format, width and plane.
 */
uint32_t drv_stride_from_format(uint32_t fourcc, uint64_t modifier, uint32_t width, size_t plane) {
    uint32_t stride_in_bytes = width;
    /* process stride of linear format, except the NXP P010 format(NV15). Because the stride in
     * byte has been calculated in drv_pixel_width_height_alignment() and the aligned_width is the
     * stride in bytes.
     */
    if ((modifier == DRM_FORMAT_MOD_LINEAR) && (fourcc != DRM_FORMAT_NV15)) {
        const struct planar_layout *layout = layout_from_format(fourcc);
        assert(plane < layout->num_planes);

        uint32_t plane_bpp = layout->bits_per_pixel[plane];
        uint32_t plane_width = DIV_ROUND_UP(width, layout->horizontal_subsampling[plane]);
        stride_in_bytes = ALIGN(plane_width * plane_bpp, 8) / 8;

        /*
         * The stride of Android YV12 buffers is required to be aligned to 16 bytes
         * (see <system/graphics.h>).
         */
        if (fourcc == DRM_FORMAT_YVU420)
            stride_in_bytes =
                    (plane == 0) ? ALIGN(stride_in_bytes, 32) : ALIGN(stride_in_bytes, 16);
    }

    return stride_in_bytes;
}

uint32_t drv_size_from_format(uint32_t fourcc, uint32_t stride, uint32_t height, size_t plane) {
    return stride * drv_height_from_format(fourcc, height, plane);
}

int drv_buffer_info_calculation_and_padding(struct gralloc_buffer_descriptor *desc,
                                            uint32_t aligned_width, uint32_t aligned_height,
                                            uint32_t fourcc, uint64_t modifier,
                                            uint32_t padding[DRV_MAX_PLANES + 1]) {
    size_t p, num_planes;
    uint32_t offset = 0;
    uint64_t aligned_size = 0; // Should align the calculation with codec2(VPU) side

    num_planes = drv_num_planes_from_format(fourcc);
    assert(num_planes);
    desc->num_planes = num_planes;

    for (p = 0; p < num_planes; p++) {
        desc->strides[p] = drv_stride_from_format(fourcc, modifier, aligned_width, p);
        desc->sizes[p] =
                drv_size_from_format(fourcc, desc->strides[p], desc->height, p) + padding[p];
        desc->offsets[p] = offset;
        offset += desc->sizes[p];

        aligned_size +=
                drv_size_from_format(fourcc, desc->strides[p], aligned_height, p) + padding[p];
    }

    desc->total_size = aligned_size + padding[DRV_MAX_PLANES];
    return 0;
}

/*
 * This function fills in the buffer object given the driver aligned stride of
 * the first plane, height and a format. This function assumes there is just
 * one kernel buffer per buffer object.
 */
int drv_buffer_info_calculation(gralloc_buffer_descriptor *desc, uint32_t aligned_width,
                                uint32_t aligned_height) {
    uint32_t padding[DRV_MAX_PLANES + 1] = {0};
    auto info = getPixleFormatInfo(desc->pixel_format);
    if (!info)
        return -1;

    if (info->is_rgb && (desc->usage & USAGE_GPU_TILED_VIV))
        padding[DRV_MAX_PLANES] = 128;

    if (info->is_yuv && (desc->usage & USAGE_PADDING_BUFFER)) {
        // hantro vpu need more buffer size for decoding
        switch (desc->pixel_format) {
            case HAL_PIXEL_FORMAT_YCbCr_420_SP:                   // NV12
            case static_cast<int32_t>(PixelFormat::YCRCB_420_SP): // NV21
            case HAL_PIXEL_FORMAT_NV12_G1_TILED:
            case HAL_PIXEL_FORMAT_NV12_G2_TILED:
            case HAL_PIXEL_FORMAT_NV12_G2_TILED_COMPRESSED:
            case HAL_PIXEL_FORMAT_P010:
            case HAL_PIXEL_FORMAT_P010_TILED_COMPRESSED:
                padding[DRV_MAX_PLANES] = aligned_width * aligned_height / 4 + 32;
                break;
            case static_cast<int32_t>(PixelFormat::YCBCR_P010):
                padding[DRV_MAX_PLANES] = aligned_width * aligned_height / 4 * 2 + 32;
                break;
            default:
                break;
        }
    }

    return drv_buffer_info_calculation_and_padding(desc, aligned_width, aligned_height,
                                                   info->fourcc, info->modifier, padding);
}
