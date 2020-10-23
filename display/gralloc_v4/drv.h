/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef DRV_H_
#define DRV_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <drm_fourcc.h>
#include <stdbool.h>
#include <stdint.h>

#define DRV_MAX_PLANES 4

// clang-format off
/* Use flags */
#define BO_USE_NONE			0
#define BO_USE_SCANOUT			(1ull << 0)
#define BO_USE_CURSOR			(1ull << 1)
#define BO_USE_CURSOR_64X64		BO_USE_CURSOR
#define BO_USE_RENDERING		(1ull << 2)
/* Skip for GBM_BO_USE_WRITE */
#define BO_USE_LINEAR			(1ull << 4)
#define BO_USE_TEXTURE			(1ull << 5)
#define BO_USE_CAMERA_WRITE		(1ull << 6)
#define BO_USE_CAMERA_READ		(1ull << 7)
#define BO_USE_PROTECTED		(1ull << 8)
#define BO_USE_SW_READ_OFTEN		(1ull << 9)
#define BO_USE_SW_READ_RARELY	        (1ull << 10)
#define BO_USE_SW_WRITE_OFTEN	        (1ull << 11)
#define BO_USE_SW_WRITE_RARELY		(1ull << 12)
#define BO_USE_HW_VIDEO_DECODER         (1ull << 13)
#define BO_USE_HW_VIDEO_ENCODER         (1ull << 14)
#define BO_USE_TEST_ALLOC		(1ull << 15)
#define BO_USE_RENDERSCRIPT		(1ull << 16)

#define BO_USE_FRAMEBUFFER              (1ull << 17)
#define BO_USE_DIMBUFFER                (1ull << 18)
#define BO_USE_HW_TEXTURE               (1ull << 19)
/*Below usage is defined for NXP i.MX device */
#define BO_USE_GPU_TILED_VIV 		0x10000000
#define BO_USE_GPU_TS_VIV 		0x20000000
/* buffer size of hantro decoder is not to yuv pixel size, it need to
 * pad some bytes for vpu usage, so add this flag */
#define BO_USE_PADDING_BUFFER 		0x40000000


/* Quirks for allocating a buffer. */
#define BO_QUIRK_NONE			0
#define BO_QUIRK_DUMB32BPP		(1ull << 0)

/* Map flags */
#define BO_MAP_NONE 0
#define BO_MAP_READ (1 << 0)
#define BO_MAP_WRITE (1 << 1)
#define BO_MAP_READ_WRITE (BO_MAP_READ | BO_MAP_WRITE)

/* This is our extension to <drm_fourcc.h>.  We need to make sure we don't step
 * on the namespace of already defined formats, which can be done by using invalid
 * fourcc codes.
 */
#define DRM_FORMAT_NONE				fourcc_code('0', '0', '0', '0')
#define DRM_FORMAT_YVU420_ANDROID		fourcc_code('9', '9', '9', '7')
#define DRM_FORMAT_FLEX_IMPLEMENTATION_DEFINED	fourcc_code('9', '9', '9', '8')
#define DRM_FORMAT_FLEX_YCbCr_420_888		fourcc_code('9', '9', '9', '9')
#define DRM_FORMAT_MTISP_SXYZW10               fourcc_code('M', 'B', '1', '0')

/* Below drm format is NXP i.MX specified format*/
#define DRM_FORMAT_I420 			0x101   // should be DRM_FORMAT_YUV420	?
#define DRM_FORMAT_NV12_TILED 			0x104
#define DRM_FORMAT_NV12_G1_TILED 		0x105
#define DRM_FORMAT_NV12_G2_TILED 		0x106
#define DRM_FORMAT_NV12_G2_TILED_COMPRESSED 	0x107
#define DRM_FORMAT_P010_TILED 			0x109
#define DRM_FORMAT_P010_TILED_COMPRESSED 	0x110
#define DRM_FORMAT_YV12 			DRM_FORMAT_YVU420_ANDROID
//#define DRM_FORMAT_NV16 			0x10
//#define DRM_FORMAT_YUYV 			0x14


struct rectangle {
        uint32_t x;
        uint32_t y;
        uint32_t width;
        uint32_t height;
};

uint32_t drv_stride_from_format(uint32_t format, uint32_t width, size_t plane);
size_t drv_num_planes_from_format(uint32_t format);

#ifdef __cplusplus
}
#endif

#endif
