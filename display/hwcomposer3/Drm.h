/*
 * Copyright (C) 2022 The Android Open Source Project
 * Copyright 2023 NXP
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
#ifndef ANDROID_HWC_DRM_H
#define ANDROID_HWC_DRM_H

#include <cstdlib>

#include "Layer.h"

namespace aidl::android::hardware::graphics::composer3::impl {

/* This is our extension to <drm_fourcc.h>.  We need to make sure we don't step
 * on the namespace of already defined formats, which can be done by using invalid
 * fourcc codes.
 */
#define DRM_FORMAT_NONE fourcc_code('0', '0', '0', '0')
#define DRM_FORMAT_YVU420_ANDROID fourcc_code('9', '9', '9', '7')
#define DRM_FORMAT_FLEX_IMPLEMENTATION_DEFINED fourcc_code('9', '9', '9', '8')
#define DRM_FORMAT_FLEX_YCbCr_420_888 fourcc_code('9', '9', '9', '9')
#define DRM_FORMAT_MTISP_SXYZW10 fourcc_code('M', 'B', '1', '0')

/* Below drm format is NXP i.MX specified format*/
#define DRM_FORMAT_I420 0x101 // should be DRM_FORMAT_YUV420	?
#define DRM_FORMAT_NV12_TILED 0x104
#define DRM_FORMAT_NV12_G1_TILED 0x105
#define DRM_FORMAT_NV12_G2_TILED 0x106
#define DRM_FORMAT_NV12_G2_TILED_COMPRESSED 0x107
#define DRM_FORMAT_P010_TILED 0x109
#define DRM_FORMAT_P010_TILED_COMPRESSED 0x110
#define DRM_FORMAT_YV12 DRM_FORMAT_YVU420_ANDROID
#define DRM_FORMAT_RAW16 0x203

enum { // defined by NXP
    FORMAT_RGBA8888 = 1,
    FORMAT_RGBX8888 = 2,
    FORMAT_RGB888 = 3,
    FORMAT_RGB565 = 4,
    FORMAT_BGRA8888 = 5,
    FORMAT_RGBA1010102 = 0x2B,
    FORMAT_RGBAFP16 = 0x16,
    FORMAT_BLOB = 0x21,
    FORMAT_YCBCR_P010 = 0x36,
    FORMAT_YV12 = 0x32315659, // YCrCb 4:2:0 Planar
    FORMAT_NV16 = 0x10,       // NV16
    FORMAT_NV21 = 0x11,       // NV21
    FORMAT_YUYV = 0x14,       // YUY2
    FORMAT_I420 = 0x101,
    FORMAT_NV12 = 0x103,
    FORMAT_NV12_TILED = 0x104,
    FORMAT_NV12_G1_TILED = 0x105,
    FORMAT_NV12_G2_TILED = 0x106,
    FORMAT_NV12_G2_TILED_COMPRESSED = 0x107,
    FORMAT_P010 = 0x108,
    FORMAT_P010_TILED = 0x109,
    FORMAT_P010_TILED_COMPRESSED = 0x110,
    FORMAT_RAW16 = 0x203,
};

enum {
    FLAGS_FRAMEBUFFER = 0x00000001,
    FLAGS_DIMBUFFER = 0x00000002,
    FLAGS_ALLOCATION_ION = 0x00000010,
    FLAGS_ALLOCATION_GPU = 0x00000020,
    FLAGS_WRAP_GPU = 0x00000040,
    FLAGS_CAMERA = 0x00100000,
    FLAGS_VIDEO = 0x00200000,
    FLAGS_UI = 0x00400000,
    FLAGS_CPU = 0x00800000,
    FLAGS_META_CHANGED = 0x01000000,
    FLAGS_HDR10_VIDEO = 0x02000000,
    FLAGS_DOLBY_VIDEO = 0x04000000,
    FLAGS_COMPRESSED_OFFSET = 0x08000000,
    FLAGS_SECURE = 0x10000000,
};

const char* GetDrmFormatString(uint32_t drm_format);

int GetDrmFormatBytesPerPixel(uint32_t drm_format);

int GetDrmFormatFromHalFormat(int hal_format);

uint32_t ConvertNxpFormatToDrmFormat(int format, uint64_t* outModifier);

char* drmGetFormatName(uint32_t format, char* outStr);

bool checkOverlayWorkaround(Layer* layer);
} // namespace aidl::android::hardware::graphics::composer3::impl

#endif
