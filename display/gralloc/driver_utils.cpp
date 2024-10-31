/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Copyright 2024 NXP
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver_utils.h"

#include <aidl/android/hardware/graphics/common/PlaneLayoutComponent.h>
#include <aidl/android/hardware/graphics/common/PlaneLayoutComponentType.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <cutils/log.h>
#include <cutils/native_handle.h>
#include <gralloctypes/Gralloc4.h>
#include <hardware/gralloc.h>
#include <inttypes.h>

#include <array>
#include <unordered_map>
#include <vector>

#include "../../include/graphics_ext.h"
#include "gralloc_helpers.h"

using aidl::android::hardware::graphics::common::PlaneLayout;
using aidl::android::hardware::graphics::common::PlaneLayoutComponent;
using aidl::android::hardware::graphics::common::PlaneLayoutComponentType;

const format_info_t formats[] = {
        {
                .id = static_cast<int32_t>(PixelFormat::RGBA_8888),
                .fourcc = DRM_FORMAT_ABGR8888,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = true,
                .is_yuv = false,
        },
        {
                .id = static_cast<int32_t>(PixelFormat::RGBX_8888),
                .fourcc = DRM_FORMAT_XBGR8888,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = true,
                .is_yuv = false,
        },
        {
                .id = static_cast<int32_t>(PixelFormat::RGB_888),
                .fourcc = DRM_FORMAT_BGR888,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = true,
                .is_yuv = false,
        },
        {
                .id = static_cast<int32_t>(PixelFormat::RGB_565),
                .fourcc = DRM_FORMAT_RGB565,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = true,
                .is_yuv = false,
        },
        {
                .id = static_cast<int32_t>(PixelFormat::BGRA_8888),
                .fourcc = DRM_FORMAT_ARGB8888,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = true,
                .is_yuv = false,
        },
        {
                .id = static_cast<int32_t>(PixelFormat::RGBA_FP16),
                .fourcc = DRM_FORMAT_ABGR16161616F,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = true,
                .is_yuv = false,
        },
        {
                .id = static_cast<int32_t>(PixelFormat::RGBA_1010102),
                .fourcc = DRM_FORMAT_ABGR2101010,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = true,
                .is_yuv = false,
        },
        {
                .id = static_cast<int32_t>(PixelFormat::YCBCR_422_SP), // NV16
                .fourcc = DRM_FORMAT_NV16,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = false,
                .is_yuv = true,
        },
        {
                .id = static_cast<int32_t>(PixelFormat::YCRCB_420_SP), // NV21
                .fourcc = DRM_FORMAT_NV21,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = false,
                .is_yuv = true,
        },
        {
                .id = static_cast<int32_t>(PixelFormat::YCBCR_422_I), // YUY2
                .fourcc = DRM_FORMAT_YUYV,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = false,
                .is_yuv = true,
        },
        {
                .id = static_cast<int32_t>(PixelFormat::YCBCR_420_888),
                .fourcc = DRM_FORMAT_NV12, // TODO: need check drm format
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = false,
                .is_yuv = true,
        },
        {
                .id = static_cast<int32_t>(PixelFormat::YV12),
                .fourcc = DRM_FORMAT_YVU420,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = false,
                .is_yuv = true,
        },
        {
                .id = static_cast<int32_t>(PixelFormat::YCBCR_P010),
                .fourcc = DRM_FORMAT_P010,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = false,
                .is_yuv = true,
        },
        {
                .id = static_cast<int32_t>(PixelFormat::BLOB),
                .fourcc = DRM_FORMAT_R8,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = false,
                .is_yuv = false,
        },
        {
                .id = static_cast<int32_t>(PixelFormat::RAW16),
                .fourcc = DRM_FORMAT_R16,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = false,
                .is_yuv = false,
        },
        {
                .id = static_cast<int32_t>(PixelFormat::Y16),
                .fourcc = DRM_FORMAT_R16,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = false,
                .is_yuv = false,
        },
        {
                .id = static_cast<int32_t>(PixelFormat::Y8),
                .fourcc = DRM_FORMAT_R8,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = false,
                .is_yuv = false,
        },
        {
                .id = static_cast<int32_t>(PixelFormat::IMPLEMENTATION_DEFINED),
                .fourcc = DRM_FORMAT_NV12,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = false,
                .is_yuv = true,
        },

        /* Following are NXP i.MX defined sepcific foramt in include/graphics_ext.h */
        {
                .id = HAL_PIXEL_FORMAT_YCbCr_422_P, // 0x100
                .fourcc = DRM_FORMAT_YUV422,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = false,
                .is_yuv = true,
        },
        {
                .id = HAL_PIXEL_FORMAT_YCbCr_420_P, // I420: 0x101
                .fourcc = DRM_FORMAT_YUV420,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = false,
                .is_yuv = true,
        },
        {
                .id = HAL_PIXEL_FORMAT_CbYCrY_422_I, // 0x102
                .fourcc = DRM_FORMAT_UYVY,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = false,
                .is_yuv = true,
        },
        {
                .id = HAL_PIXEL_FORMAT_YCbCr_420_SP, // NV12: 0x103
                .fourcc = DRM_FORMAT_NV12,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = false,
                .is_yuv = true,
        },
        {
                .id = HAL_PIXEL_FORMAT_NV12_TILED, // 0x104
                .fourcc = DRM_FORMAT_NV12,
                .modifier = DRM_FORMAT_MOD_AMPHION_TILED,
                .is_rgb = false,
                .is_yuv = true,
        },
        {
                .id = HAL_PIXEL_FORMAT_NV12_G1_TILED, // 0x105
                .fourcc = DRM_FORMAT_NV12,
                .modifier = DRM_FORMAT_MOD_VSI_G1_TILED,
                .is_rgb = false,
                .is_yuv = true,
        },
        {
                .id = HAL_PIXEL_FORMAT_NV12_G2_TILED, // 0x106
                .fourcc = DRM_FORMAT_NV12,
                .modifier = DRM_FORMAT_MOD_VSI_G2_TILED,
                .is_rgb = false,
                .is_yuv = true,
        },
        {
                .id = HAL_PIXEL_FORMAT_NV12_G2_TILED_COMPRESSED, // 0x107
                .fourcc = DRM_FORMAT_NV12,
                .modifier = DRM_FORMAT_MOD_VSI_G2_TILED_COMPRESSED,
                .is_rgb = false,
                .is_yuv = true,
        },
        {
                .id = HAL_PIXEL_FORMAT_P010, // 0x108
                .fourcc = DRM_FORMAT_NV15,
                .modifier = DRM_FORMAT_MOD_LINEAR,
                .is_rgb = false,
                .is_yuv = true,
        },
        {
                .id = HAL_PIXEL_FORMAT_P010_TILED, // 0x109
                .fourcc = DRM_FORMAT_NV15,
                .modifier = DRM_FORMAT_MOD_VSI_G1_TILED,
                .is_rgb = false,
                .is_yuv = true,
        },
        {
                .id = HAL_PIXEL_FORMAT_P010_TILED_COMPRESSED, // 0x110
                .fourcc = DRM_FORMAT_NV15,
                .modifier = DRM_FORMAT_MOD_VSI_G2_TILED_COMPRESSED,
                .is_rgb = false,
                .is_yuv = true,
        },
};

std::string getDrmFormatString(uint32_t drmFormat) {
    char* sequence = (char*)&drmFormat;
    std::string s(sequence, 4);
    return "DRM_FOURCC_" + s;
}

std::string getPixelFormatString(int32_t format) {
    return aidl::android::hardware::graphics::common::toString(static_cast<PixelFormat>(format));
}

std::string getUsageString(uint64_t usage) {
    std::vector<std::string> usages;
    if (usage & static_cast<uint64_t>(BufferUsage::CPU_READ_OFTEN)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::CPU_READ_OFTEN);
        usages.push_back("CPU_READ_OFTEN");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::CPU_READ_RARELY)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::CPU_READ_RARELY);
        usages.push_back("CPU_READ_RARELY");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::CPU_WRITE_OFTEN)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::CPU_WRITE_OFTEN);
        usages.push_back("CPU_WRITE_OFTEN");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::CPU_WRITE_RARELY)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::CPU_WRITE_RARELY);
        usages.push_back("CPU_WRITE_RARELY");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::GPU_TEXTURE)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::GPU_TEXTURE);
        usages.push_back("GPU_TEXTURE");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::GPU_RENDER_TARGET)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::GPU_RENDER_TARGET);
        usages.push_back("GPU_RENDER_TARGET");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::COMPOSER_OVERLAY)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::COMPOSER_OVERLAY);
        usages.push_back("COMPOSER_OVERLAY");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::COMPOSER_CLIENT_TARGET)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::COMPOSER_CLIENT_TARGET);
        usages.push_back("COMPOSER_CLIENT_TARGET");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::PROTECTED)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::PROTECTED);
        usages.push_back("PROTECTED");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::COMPOSER_CURSOR)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::COMPOSER_CURSOR);
        usages.push_back("COMPOSER_CURSOR");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::VIDEO_ENCODER)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::VIDEO_ENCODER);
        usages.push_back("VIDEO_ENCODER");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::CAMERA_OUTPUT)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::CAMERA_OUTPUT);
        usages.push_back("CAMERA_OUTPUT");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::CAMERA_INPUT)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::CAMERA_INPUT);
        usages.push_back("CAMERA_INPUT");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::RENDERSCRIPT)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::RENDERSCRIPT);
        usages.push_back("RENDERSCRIPT");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::VIDEO_DECODER)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::VIDEO_DECODER);
        usages.push_back("VIDEO_DECODER");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::SENSOR_DIRECT_DATA)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::SENSOR_DIRECT_DATA);
        usages.push_back("SENSOR_DIRECT_DATA");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::GPU_DATA_BUFFER)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::GPU_DATA_BUFFER);
        usages.push_back("GPU_DATA_BUFFER");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::GPU_CUBE_MAP)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::GPU_CUBE_MAP);
        usages.push_back("GPU_CUBE_MAP");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::GPU_MIPMAP_COMPLETE)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::GPU_MIPMAP_COMPLETE);
        usages.push_back("GPU_MIPMAP_COMPLETE");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::HW_IMAGE_ENCODER)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::HW_IMAGE_ENCODER);
        usages.push_back("HW_IMAGE_ENCODER");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::FRONT_BUFFER)) {
        usage &= ~static_cast<uint64_t>(BufferUsage::FRONT_BUFFER);
        usages.push_back("FRONT_BUFFER");
    }

    if (usage & GRALLOC_USAGE_PRIVATE_0) {
        usage &= ~static_cast<uint64_t>(GRALLOC_USAGE_PRIVATE_0);
        usages.push_back("PRIVATE_0");
    }
    if (usage & GRALLOC_USAGE_PRIVATE_1) {
        usage &= ~static_cast<uint64_t>(GRALLOC_USAGE_PRIVATE_1);
        usages.push_back("PRIVATE_1");
    }
    if (usage & GRALLOC_USAGE_PRIVATE_2) {
        usage &= ~static_cast<uint64_t>(GRALLOC_USAGE_PRIVATE_2);
        usages.push_back("PRIVATE_2");
    }
    if (usage & GRALLOC_USAGE_PRIVATE_3) {
        usage &= ~static_cast<uint64_t>(GRALLOC_USAGE_PRIVATE_3);
        usages.push_back("PRIVATE_3");
    }
    if (usage & static_cast<uint64_t>(BufferUsage::VENDOR_MASK)) {
        usages.push_back(android::base::StringPrintf("UnknownUsageBits-%" PRIu64, usage));
    }
    if (usage & static_cast<uint64_t>(BufferUsage::VENDOR_MASK_HI)) {
        usages.push_back(android::base::StringPrintf("UnknownUsageHiBits-%" PRIu64, usage));
    }

    return android::base::Join(usages, '|');
}

const struct format_info_t* getPixleFormatInfo(int32_t pixel_format) {
    for (const auto& format : formats) {
        if (format.id == pixel_format)
            return &format;
    }

    ALOGE("%s: Cannot support pixel format:%" PRIx32, __func__, pixel_format);
    return nullptr;
}

int convertToBufferFlags(uint64_t grallocUsage, uint32_t* outBufferFlags) {
    uint32_t bufferFlags = 0;

    if (grallocUsage & GRALLOC_USAGE_HW_FB) {
        bufferFlags |= NXP_GRALLOC_FLAGS_FRAMEBUFFER;
    }

    *outBufferFlags |= bufferFlags;
    return 0;
}

int convertToHalDescriptor(const BufferDescriptorInfoV4& descriptor,
                           struct gralloc_buffer_descriptor* outDescriptor) {
    outDescriptor->name = descriptor.name;
    outDescriptor->width = descriptor.width;
    outDescriptor->height = descriptor.height;
    outDescriptor->layer_count = descriptor.layerCount;
    outDescriptor->pixel_format = static_cast<int32_t>(descriptor.format);
    outDescriptor->usage = descriptor.usage;
    outDescriptor->reserved_region_size = descriptor.reservedSize;

#ifdef FRAMEBUFFER_WITH_TILED_COMPRESSION
    outDescriptor->flags |= NXP_GRALLOC_FLAGS_TILED_FRAMEBUFFER;
#endif
#ifdef WORKAROUND_DISPLAY_UNDERRUN
    outDescriptor->flags |= NXP_GRALLOC_FLAGS_DISPLAY_UNDERRUN;
#endif
    if (descriptor.layerCount > 1) {
        ALOGE("%s layerCount=%d > 1 is unsupported", __func__, descriptor.layerCount);
        return -1;
    }
    auto info = getPixleFormatInfo(outDescriptor->pixel_format);
    if (!info) {
        std::string pixelFormatString = getPixelFormatString(outDescriptor->pixel_format);
        ALOGE("%s Unsupported format %s", __func__, pixelFormatString.c_str());
        return -1;
    } else {
        outDescriptor->drm_format = info->fourcc;
        outDescriptor->modifier = info->modifier;
    }

    if (convertToBufferFlags(descriptor.usage, &outDescriptor->flags)) {
        std::string usageString = getUsageString(descriptor.usage);
        ALOGE("%s Unsupported usage flags %s", __func__, usageString.c_str());
        return -1;
    }
    return 0;
}

const std::unordered_map<uint32_t, std::vector<PlaneLayout>>& GetPlaneLayoutsMap() {
    static const auto* kPlaneLayoutsMap =
            new std::unordered_map<uint32_t, std::vector<PlaneLayout>>({
                    {DRM_FORMAT_ABGR8888,
                     {{
                             .components = {{.type = android::gralloc4::PlaneLayoutComponentType_R,
                                             .offsetInBits = 0,
                                             .sizeInBits = 8},
                                            {.type = android::gralloc4::PlaneLayoutComponentType_G,
                                             .offsetInBits = 8,
                                             .sizeInBits = 8},
                                            {.type = android::gralloc4::PlaneLayoutComponentType_B,
                                             .offsetInBits = 16,
                                             .sizeInBits = 8},
                                            {.type = android::gralloc4::PlaneLayoutComponentType_A,
                                             .offsetInBits = 24,
                                             .sizeInBits = 8}},
                             .sampleIncrementInBits = 32,
                             .horizontalSubsampling = 1,
                             .verticalSubsampling = 1,
                     }}},

                    {DRM_FORMAT_ABGR2101010,
                     {{
                             .components = {{.type = android::gralloc4::PlaneLayoutComponentType_R,
                                             .offsetInBits = 0,
                                             .sizeInBits = 10},
                                            {.type = android::gralloc4::PlaneLayoutComponentType_G,
                                             .offsetInBits = 10,
                                             .sizeInBits = 10},
                                            {.type = android::gralloc4::PlaneLayoutComponentType_B,
                                             .offsetInBits = 20,
                                             .sizeInBits = 10},
                                            {.type = android::gralloc4::PlaneLayoutComponentType_A,
                                             .offsetInBits = 30,
                                             .sizeInBits = 2}},
                             .sampleIncrementInBits = 32,
                             .horizontalSubsampling = 1,
                             .verticalSubsampling = 1,
                     }}},

                    {DRM_FORMAT_ABGR16161616F,
                     {{
                             .components = {{.type = android::gralloc4::PlaneLayoutComponentType_R,
                                             .offsetInBits = 0,
                                             .sizeInBits = 16},
                                            {.type = android::gralloc4::PlaneLayoutComponentType_G,
                                             .offsetInBits = 16,
                                             .sizeInBits = 16},
                                            {.type = android::gralloc4::PlaneLayoutComponentType_B,
                                             .offsetInBits = 32,
                                             .sizeInBits = 16},
                                            {.type = android::gralloc4::PlaneLayoutComponentType_A,
                                             .offsetInBits = 48,
                                             .sizeInBits = 16}},
                             .sampleIncrementInBits = 64,
                             .horizontalSubsampling = 1,
                             .verticalSubsampling = 1,
                     }}},

                    {DRM_FORMAT_ARGB8888,
                     {{
                             .components = {{.type = android::gralloc4::PlaneLayoutComponentType_B,
                                             .offsetInBits = 0,
                                             .sizeInBits = 8},
                                            {.type = android::gralloc4::PlaneLayoutComponentType_G,
                                             .offsetInBits = 8,
                                             .sizeInBits = 8},
                                            {.type = android::gralloc4::PlaneLayoutComponentType_R,
                                             .offsetInBits = 16,
                                             .sizeInBits = 8},
                                            {.type = android::gralloc4::PlaneLayoutComponentType_A,
                                             .offsetInBits = 24,
                                             .sizeInBits = 8}},
                             .sampleIncrementInBits = 32,
                             .horizontalSubsampling = 1,
                             .verticalSubsampling = 1,
                     }}},

                    {DRM_FORMAT_NV12,
                     {{
                              .components = {{.type = android::gralloc4::PlaneLayoutComponentType_Y,
                                              .offsetInBits = 0,
                                              .sizeInBits = 8}},
                              .sampleIncrementInBits = 8,
                              .horizontalSubsampling = 1,
                              .verticalSubsampling = 1,
                      },
                      {
                              .components =
                                      {{.type = android::gralloc4::PlaneLayoutComponentType_CB,
                                        .offsetInBits = 0,
                                        .sizeInBits = 8},
                                       {.type = android::gralloc4::PlaneLayoutComponentType_CR,
                                        .offsetInBits = 8,
                                        .sizeInBits = 8}},
                              .sampleIncrementInBits = 16,
                              .horizontalSubsampling = 2,
                              .verticalSubsampling = 2,
                      }}},

                    {DRM_FORMAT_NV21,
                     {{
                              .components = {{.type = android::gralloc4::PlaneLayoutComponentType_Y,
                                              .offsetInBits = 0,
                                              .sizeInBits = 8}},
                              .sampleIncrementInBits = 8,
                              .horizontalSubsampling = 1,
                              .verticalSubsampling = 1,
                      },
                      {
                              .components =
                                      {{.type = android::gralloc4::PlaneLayoutComponentType_CR,
                                        .offsetInBits = 0,
                                        .sizeInBits = 8},
                                       {.type = android::gralloc4::PlaneLayoutComponentType_CB,
                                        .offsetInBits = 8,
                                        .sizeInBits = 8}},
                              .sampleIncrementInBits = 16,
                              .horizontalSubsampling = 2,
                              .verticalSubsampling = 2,
                      }}},

                    {DRM_FORMAT_P010,
                     {{
                              .components = {{.type = android::gralloc4::PlaneLayoutComponentType_Y,
                                              .offsetInBits = 6,
                                              .sizeInBits = 10}},
                              .sampleIncrementInBits = 16,
                              .horizontalSubsampling = 1,
                              .verticalSubsampling = 1,
                      },
                      {
                              .components =
                                      {{.type = android::gralloc4::PlaneLayoutComponentType_CB,
                                        .offsetInBits = 6,
                                        .sizeInBits = 10},
                                       {.type = android::gralloc4::PlaneLayoutComponentType_CR,
                                        .offsetInBits = 22,
                                        .sizeInBits = 10}},
                              .sampleIncrementInBits = 32,
                              .horizontalSubsampling = 2,
                              .verticalSubsampling = 2,
                      }}},

                    {DRM_FORMAT_R8,
                     {{
                             .components = {{.type = android::gralloc4::PlaneLayoutComponentType_R,
                                             .offsetInBits = 0,
                                             .sizeInBits = 8}},
                             .sampleIncrementInBits = 8,
                             .horizontalSubsampling = 1,
                             .verticalSubsampling = 1,
                     }}},

                    {DRM_FORMAT_R16,
                     {{
                             .components = {{.type = android::gralloc4::PlaneLayoutComponentType_R,
                                             .offsetInBits = 0,
                                             .sizeInBits = 16}},
                             .sampleIncrementInBits = 16,
                             .horizontalSubsampling = 1,
                             .verticalSubsampling = 1,
                     }}},

                    {DRM_FORMAT_RGB565,
                     {{
                             .components = {{.type = android::gralloc4::PlaneLayoutComponentType_R,
                                             .offsetInBits = 0,
                                             .sizeInBits = 5},
                                            {.type = android::gralloc4::PlaneLayoutComponentType_G,
                                             .offsetInBits = 5,
                                             .sizeInBits = 6},
                                            {.type = android::gralloc4::PlaneLayoutComponentType_B,
                                             .offsetInBits = 11,
                                             .sizeInBits = 5}},
                             .sampleIncrementInBits = 16,
                             .horizontalSubsampling = 1,
                             .verticalSubsampling = 1,
                     }}},

                    {DRM_FORMAT_BGR888,

                     {{
                             .components = {{.type = android::gralloc4::PlaneLayoutComponentType_R,
                                             .offsetInBits = 0,
                                             .sizeInBits = 8},
                                            {.type = android::gralloc4::PlaneLayoutComponentType_G,
                                             .offsetInBits = 8,
                                             .sizeInBits = 8},
                                            {.type = android::gralloc4::PlaneLayoutComponentType_B,
                                             .offsetInBits = 16,
                                             .sizeInBits = 8}},
                             .sampleIncrementInBits = 24,
                             .horizontalSubsampling = 1,
                             .verticalSubsampling = 1,
                     }}},

                    {DRM_FORMAT_XBGR8888,
                     {{
                             .components = {{.type = android::gralloc4::PlaneLayoutComponentType_B,
                                             .offsetInBits = 0,
                                             .sizeInBits = 8},
                                            {.type = android::gralloc4::PlaneLayoutComponentType_G,
                                             .offsetInBits = 8,
                                             .sizeInBits = 8},
                                            {.type = android::gralloc4::PlaneLayoutComponentType_R,
                                             .offsetInBits = 16,
                                             .sizeInBits = 8}},
                             .sampleIncrementInBits = 32,
                             .horizontalSubsampling = 1,
                             .verticalSubsampling = 1,
                     }}},

                    {DRM_FORMAT_YUV420, // I420
                     {
                             {
                                     .components = {{.type = android::gralloc4::
                                                             PlaneLayoutComponentType_Y,
                                                     .offsetInBits = 0,
                                                     .sizeInBits = 8}},
                                     .sampleIncrementInBits = 8,
                                     .horizontalSubsampling = 1,
                                     .verticalSubsampling = 1,
                             },
                             {
                                     .components = {{.type = android::gralloc4::
                                                             PlaneLayoutComponentType_CB,
                                                     .offsetInBits = 0,
                                                     .sizeInBits = 8}},
                                     .sampleIncrementInBits = 8,
                                     .horizontalSubsampling = 2,
                                     .verticalSubsampling = 2,
                             },
                             {
                                     .components = {{.type = android::gralloc4::
                                                             PlaneLayoutComponentType_CR,
                                                     .offsetInBits = 0,
                                                     .sizeInBits = 8}},
                                     .sampleIncrementInBits = 8,
                                     .horizontalSubsampling = 2,
                                     .verticalSubsampling = 2,
                             },
                     }},

                    {DRM_FORMAT_YVU420, // YV12
                     {
                             {
                                     .components = {{.type = android::gralloc4::
                                                             PlaneLayoutComponentType_Y,
                                                     .offsetInBits = 0,
                                                     .sizeInBits = 8}},
                                     .sampleIncrementInBits = 8,
                                     .horizontalSubsampling = 1,
                                     .verticalSubsampling = 1,
                             },
                             {
                                     .components = {{.type = android::gralloc4::
                                                             PlaneLayoutComponentType_CR,
                                                     .offsetInBits = 0,
                                                     .sizeInBits = 8}},
                                     .sampleIncrementInBits = 8,
                                     .horizontalSubsampling = 2,
                                     .verticalSubsampling = 2,
                             },
                             {
                                     .components = {{.type = android::gralloc4::
                                                             PlaneLayoutComponentType_CB,
                                                     .offsetInBits = 0,
                                                     .sizeInBits = 8}},
                                     .sampleIncrementInBits = 8,
                                     .horizontalSubsampling = 2,
                                     .verticalSubsampling = 2,
                             },
                     }},

                    {DRM_FORMAT_YUYV,
                     {
                             {
                                     .components = {{.type = android::gralloc4::
                                                             PlaneLayoutComponentType_Y,
                                                     .offsetInBits = 0,
                                                     .sizeInBits = 8},
                                                    {.type = android::gralloc4::
                                                             PlaneLayoutComponentType_CB,
                                                     .offsetInBits = 8,
                                                     .sizeInBits = 8},
                                                    {.type = android::gralloc4::
                                                             PlaneLayoutComponentType_CR,
                                                     .offsetInBits = 8,
                                                     .sizeInBits = 8}},
                                     .sampleIncrementInBits = 8,
                                     .horizontalSubsampling = 1,
                                     .verticalSubsampling = 1,
                             },
                     }},

                    {DRM_FORMAT_NV16, // HAL_PIXEL_FORMAT_YCbCr_422_SP
                     {{
                              .components = {{.type = android::gralloc4::PlaneLayoutComponentType_Y,
                                              .offsetInBits = 0,
                                              .sizeInBits = 8}},
                              .sampleIncrementInBits = 8,
                              .horizontalSubsampling = 1,
                              .verticalSubsampling = 1,
                      },
                      {
                              .components =
                                      {{.type = android::gralloc4::PlaneLayoutComponentType_CB,
                                        .offsetInBits = 0,
                                        .sizeInBits = 8},
                                       {.type = android::gralloc4::PlaneLayoutComponentType_CR,
                                        .offsetInBits = 8,
                                        .sizeInBits = 8}},
                              .sampleIncrementInBits = 16,
                              .horizontalSubsampling = 2,
                              .verticalSubsampling = 1,
                      }}},

                    {DRM_FORMAT_R16,
                     {
                             {
                                     .components = {{.type = android::gralloc4::
                                                             PlaneLayoutComponentType_RAW,
                                                     .offsetInBits = 0,
                                                     .sizeInBits = 16}},
                                     .sampleIncrementInBits = 16,
                                     .horizontalSubsampling = 1,
                                     .verticalSubsampling = 1,
                             },
                     }},
            });
    return *kPlaneLayoutsMap;
}

int getPlaneLayouts(uint32_t drmFormat, std::vector<PlaneLayout>* outPlaneLayouts) {
    const auto& planeLayoutsMap = GetPlaneLayoutsMap();
    const auto it = planeLayoutsMap.find(drmFormat);
    if (it == planeLayoutsMap.end()) {
        ALOGE("%s Unknown plane layout for format 0x%x", __func__, drmFormat);
        return -1;
    }

    *outPlaneLayouts = it->second;
    return 0;
}
