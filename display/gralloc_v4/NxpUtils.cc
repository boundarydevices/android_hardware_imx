/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "NxpUtils.h"

#include <DisplayUtil.h>
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

#include "../../include/graphics_ext.h"
#include "drv.h"
#include "gralloc_helpers.h"

using aidl::android::hardware::graphics::common::PlaneLayout;
using aidl::android::hardware::graphics::common::PlaneLayoutComponent;
using aidl::android::hardware::graphics::common::PlaneLayoutComponentType;
using android::hardware::hidl_bitfield;
using android::hardware::hidl_handle;
using android::hardware::graphics::common::V1_2::BufferUsage;
using android::hardware::graphics::common::V1_2::PixelFormat;

using BufferDescriptorInfo =
        android::hardware::graphics::mapper::V4_0::IMapper::BufferDescriptorInfo;

std::string getDrmFormatString(uint32_t drmFormat) {
    switch (drmFormat) {
        case DRM_FORMAT_ABGR1555:
            return "DRM_FORMAT_ABGR1555";
        case DRM_FORMAT_ABGR2101010:
            return "DRM_FORMAT_ABGR2101010";
        case DRM_FORMAT_ABGR4444:
            return "DRM_FORMAT_ABGR4444";
        case DRM_FORMAT_ABGR8888:
            return "DRM_FORMAT_ABGR8888";
        case DRM_FORMAT_ARGB1555:
            return "DRM_FORMAT_ARGB1555";
        case DRM_FORMAT_ARGB2101010:
            return "DRM_FORMAT_ARGB2101010";
        case DRM_FORMAT_ARGB4444:
            return "DRM_FORMAT_ARGB4444";
        case DRM_FORMAT_ARGB8888:
            return "DRM_FORMAT_ARGB8888";
        case DRM_FORMAT_AYUV:
            return "DRM_FORMAT_AYUV";
        case DRM_FORMAT_BGR233:
            return "DRM_FORMAT_BGR233";
        case DRM_FORMAT_BGR565:
            return "DRM_FORMAT_BGR565";
        case DRM_FORMAT_BGR888:
            return "DRM_FORMAT_BGR888";
        case DRM_FORMAT_BGRA1010102:
            return "DRM_FORMAT_BGRA1010102";
        case DRM_FORMAT_BGRA4444:
            return "DRM_FORMAT_BGRA4444";
        case DRM_FORMAT_BGRA5551:
            return "DRM_FORMAT_BGRA5551";
        case DRM_FORMAT_BGRA8888:
            return "DRM_FORMAT_BGRA8888";
        case DRM_FORMAT_BGRX1010102:
            return "DRM_FORMAT_BGRX1010102";
        case DRM_FORMAT_BGRX4444:
            return "DRM_FORMAT_BGRX4444";
        case DRM_FORMAT_BGRX5551:
            return "DRM_FORMAT_BGRX5551";
        case DRM_FORMAT_BGRX8888:
            return "DRM_FORMAT_BGRX8888";
        case DRM_FORMAT_C8:
            return "DRM_FORMAT_C8";
        case DRM_FORMAT_GR88:
            return "DRM_FORMAT_GR88";
        case DRM_FORMAT_NV12:
            return "DRM_FORMAT_NV12";
        case DRM_FORMAT_NV21:
            return "DRM_FORMAT_NV21";
        case DRM_FORMAT_R8:
            return "DRM_FORMAT_R8";
        case DRM_FORMAT_RG88:
            return "DRM_FORMAT_RG88";
        case DRM_FORMAT_RGB332:
            return "DRM_FORMAT_RGB332";
        case DRM_FORMAT_RGB565:
            return "DRM_FORMAT_RGB565";
        case DRM_FORMAT_RGB888:
            return "DRM_FORMAT_RGB888";
        case DRM_FORMAT_RGBA1010102:
            return "DRM_FORMAT_RGBA1010102";
        case DRM_FORMAT_RGBA4444:
            return "DRM_FORMAT_RGBA4444";
        case DRM_FORMAT_RGBA5551:
            return "DRM_FORMAT_RGBA5551";
        case DRM_FORMAT_RGBA8888:
            return "DRM_FORMAT_RGBA8888";
        case DRM_FORMAT_RGBX1010102:
            return "DRM_FORMAT_RGBX1010102";
        case DRM_FORMAT_RGBX4444:
            return "DRM_FORMAT_RGBX4444";
        case DRM_FORMAT_RGBX5551:
            return "DRM_FORMAT_RGBX5551";
        case DRM_FORMAT_RGBX8888:
            return "DRM_FORMAT_RGBX8888";
        case DRM_FORMAT_UYVY:
            return "DRM_FORMAT_UYVY";
        case DRM_FORMAT_VYUY:
            return "DRM_FORMAT_VYUY";
        case DRM_FORMAT_XBGR1555:
            return "DRM_FORMAT_XBGR1555";
        case DRM_FORMAT_XBGR2101010:
            return "DRM_FORMAT_XBGR2101010";
        case DRM_FORMAT_XBGR4444:
            return "DRM_FORMAT_XBGR4444";
        case DRM_FORMAT_XBGR8888:
            return "DRM_FORMAT_XBGR8888";
        case DRM_FORMAT_XRGB1555:
            return "DRM_FORMAT_XRGB1555";
        case DRM_FORMAT_XRGB2101010:
            return "DRM_FORMAT_XRGB2101010";
        case DRM_FORMAT_XRGB4444:
            return "DRM_FORMAT_XRGB4444";
        case DRM_FORMAT_XRGB8888:
            return "DRM_FORMAT_XRGB8888";
        case DRM_FORMAT_YUYV:
            return "DRM_FORMAT_YUYV";
        case DRM_FORMAT_YVU420:
            return "DRM_FORMAT_YVU420";
        case DRM_FORMAT_YVYU:
            return "DRM_FORMAT_YVYU";
        case DRM_FORMAT_RAW16:
            return "DRM_FORMAT_RAW16";
    }
    return android::base::StringPrintf("Unknown(%d)", drmFormat);
}

std::string getPixelFormatString(PixelFormat format) {
    switch (format) {
        case PixelFormat::BGRA_8888:
            return "PixelFormat::BGRA_8888";
        case PixelFormat::BLOB:
            return "PixelFormat::BLOB";
        case PixelFormat::DEPTH_16:
            return "PixelFormat::DEPTH_16";
        case PixelFormat::DEPTH_24:
            return "PixelFormat::DEPTH_24";
        case PixelFormat::DEPTH_24_STENCIL_8:
            return "PixelFormat::DEPTH_24_STENCIL_8";
        case PixelFormat::DEPTH_32F:
            return "PixelFormat::DEPTH_24";
        case PixelFormat::DEPTH_32F_STENCIL_8:
            return "PixelFormat::DEPTH_24_STENCIL_8";
        case PixelFormat::HSV_888:
            return "PixelFormat::HSV_888";
        case PixelFormat::IMPLEMENTATION_DEFINED:
            return "PixelFormat::IMPLEMENTATION_DEFINED";
        case PixelFormat::RAW10:
            return "PixelFormat::RAW10";
        case PixelFormat::RAW12:
            return "PixelFormat::RAW12";
        case PixelFormat::RAW16:
            return "PixelFormat::RAW16";
        case PixelFormat::RAW_OPAQUE:
            return "PixelFormat::RAW_OPAQUE";
        case PixelFormat::RGBA_1010102:
            return "PixelFormat::RGBA_1010102";
        case PixelFormat::RGBA_8888:
            return "PixelFormat::RGBA_8888";
        case PixelFormat::RGBA_FP16:
            return "PixelFormat::RGBA_FP16";
        case PixelFormat::RGBX_8888:
            return "PixelFormat::RGBX_8888";
        case PixelFormat::RGB_565:
            return "PixelFormat::RGB_565";
        case PixelFormat::RGB_888:
            return "PixelFormat::RGB_888";
        case PixelFormat::STENCIL_8:
            return "PixelFormat::STENCIL_8";
        case PixelFormat::Y16:
            return "PixelFormat::Y16";
        case PixelFormat::Y8:
            return "PixelFormat::Y8";
        case PixelFormat::YCBCR_420_888:
            return "PixelFormat::YCBCR_420_888";
        case PixelFormat::YCBCR_422_I:
            return "PixelFormat::YCBCR_422_I";
        case PixelFormat::YCBCR_422_SP:
            return "PixelFormat::YCBCR_422_SP";
        case PixelFormat::YCBCR_P010:
            return "PixelFormat::YCBCR_P010";
        case PixelFormat::YCRCB_420_SP:
            return "PixelFormat::YCRCB_420_SP";
        case PixelFormat::YV12:
            return "PixelFormat::YV12";
    }
    return android::base::StringPrintf("PixelFormat::Unknown(0x%x)", static_cast<uint32_t>(format));
}

int convertToDrmFormat(PixelFormat format, uint32_t* outDrmFormat) {
    switch (format) {
        case PixelFormat::BGRA_8888:
            *outDrmFormat = DRM_FORMAT_ARGB8888;
            return 0;
        /**
         * Choose DRM_FORMAT_R8 because <system/graphics.h> requires the buffers
         * with a format HAL_PIXEL_FORMAT_BLOB have a height of 1, and width
         * equal to their size in bytes.
         */
        case PixelFormat::BLOB:
            *outDrmFormat = DRM_FORMAT_R8;
            return 0;
        case PixelFormat::DEPTH_16:
            return -EINVAL;
        case PixelFormat::DEPTH_24:
            return -EINVAL;
        case PixelFormat::DEPTH_24_STENCIL_8:
            return -EINVAL;
        case PixelFormat::DEPTH_32F:
            return -EINVAL;
        case PixelFormat::DEPTH_32F_STENCIL_8:
            return -EINVAL;
        case PixelFormat::HSV_888:
            return -EINVAL;
        case PixelFormat::IMPLEMENTATION_DEFINED:
            *outDrmFormat = DRM_FORMAT_FLEX_IMPLEMENTATION_DEFINED;
            return 0;
        case PixelFormat::RAW10:
            return -EINVAL;
        case PixelFormat::RAW12:
            return -EINVAL;
        case PixelFormat::RAW16:
            *outDrmFormat = DRM_FORMAT_RAW16;
            return 0;
        /* TODO use blob */
        case PixelFormat::RAW_OPAQUE:
            return -EINVAL;
        case PixelFormat::RGBA_1010102:
            *outDrmFormat = DRM_FORMAT_ABGR2101010;
            return 0;
        case PixelFormat::RGBA_8888:
            *outDrmFormat = DRM_FORMAT_ABGR8888;
            return 0;
        case PixelFormat::RGBA_FP16:
            *outDrmFormat = DRM_FORMAT_ABGR16161616F;
            return 0;
        case PixelFormat::RGBX_8888:
            *outDrmFormat = DRM_FORMAT_XBGR8888;
            return 0;
        case PixelFormat::RGB_565:
            *outDrmFormat = DRM_FORMAT_RGB565;
            return 0;
        case PixelFormat::RGB_888:
            *outDrmFormat = DRM_FORMAT_BGR888;
            return 0;
        case PixelFormat::STENCIL_8:
            return -EINVAL;
        case PixelFormat::Y16:
            *outDrmFormat = DRM_FORMAT_R16;
            return 0;
        case PixelFormat::Y8:
            *outDrmFormat = DRM_FORMAT_R8;
            return 0;
        case PixelFormat::YCBCR_420_888:
            *outDrmFormat = DRM_FORMAT_FLEX_YCbCr_420_888;
            return 0;
        case PixelFormat::YCBCR_422_SP:
            *outDrmFormat = DRM_FORMAT_NV16;
            return 0;
        case PixelFormat::YCBCR_422_I:
            *outDrmFormat = DRM_FORMAT_YUYV;
            return 0;
        case PixelFormat::YCBCR_P010:
            *outDrmFormat = DRM_FORMAT_P010;
            return 0;
        case PixelFormat::YCRCB_420_SP:
            *outDrmFormat = DRM_FORMAT_NV21;
            return 0;
        case PixelFormat::YV12:
            *outDrmFormat = DRM_FORMAT_YVU420_ANDROID;
            return 0;
    };

    uint32_t gralloc_format = static_cast<uint32_t>(format);
    switch (gralloc_format) {
        /* Below are NXP i.MX specified format*/
        case HAL_PIXEL_FORMAT_YCbCr_422_P:
            *outDrmFormat = DRM_FORMAT_YUV422;
            return 0;
        case HAL_PIXEL_FORMAT_YCbCr_420_P:
            *outDrmFormat = DRM_FORMAT_YUV420; // same as FORMAT_I420  ?
            return 0;
        case HAL_PIXEL_FORMAT_CbYCrY_422_I:
            *outDrmFormat = DRM_FORMAT_UYVY;
            return 0;
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            *outDrmFormat = DRM_FORMAT_NV12;
            return 0;
        case HAL_PIXEL_FORMAT_NV12_TILED:
            *outDrmFormat = DRM_FORMAT_NV12_TILED;
            return 0;
        case HAL_PIXEL_FORMAT_NV12_G1_TILED:
            *outDrmFormat = DRM_FORMAT_NV12_G1_TILED;
            return 0;
        case HAL_PIXEL_FORMAT_NV12_G2_TILED:
            *outDrmFormat = DRM_FORMAT_NV12_G2_TILED;
            return 0;
        case HAL_PIXEL_FORMAT_NV12_G2_TILED_COMPRESSED:
            *outDrmFormat = DRM_FORMAT_NV12_G2_TILED_COMPRESSED;
            return 0;
        case HAL_PIXEL_FORMAT_P010:
            *outDrmFormat = DRM_FORMAT_P010;
            return 0;
        case HAL_PIXEL_FORMAT_P010_TILED:
            *outDrmFormat = DRM_FORMAT_P010_TILED;
            return 0;
        case HAL_PIXEL_FORMAT_P010_TILED_COMPRESSED:
            *outDrmFormat = DRM_FORMAT_P010_TILED_COMPRESSED;
            return 0;
    };

    return -EINVAL;
}

int convertToBufferUsage(uint64_t grallocUsage, uint64_t* outBufferUsage) {
    uint64_t bufferUsage = BO_USE_NONE;

    if ((grallocUsage & BufferUsage::CPU_READ_MASK) ==
        static_cast<uint64_t>(BufferUsage::CPU_READ_RARELY)) {
        bufferUsage |= BO_USE_SW_READ_RARELY;
    }
    if ((grallocUsage & BufferUsage::CPU_READ_MASK) ==
        static_cast<uint64_t>(BufferUsage::CPU_READ_OFTEN)) {
        bufferUsage |= BO_USE_SW_READ_OFTEN;
    }
    if ((grallocUsage & BufferUsage::CPU_WRITE_MASK) ==
        static_cast<uint64_t>(BufferUsage::CPU_WRITE_RARELY)) {
        bufferUsage |= BO_USE_SW_WRITE_RARELY;
    }
    if ((grallocUsage & BufferUsage::CPU_WRITE_MASK) ==
        static_cast<uint64_t>(BufferUsage::CPU_WRITE_OFTEN)) {
        bufferUsage |= BO_USE_SW_WRITE_OFTEN;
    }
    if (grallocUsage & BufferUsage::GPU_TEXTURE) {
        bufferUsage |= BO_USE_TEXTURE;
    }
    if (grallocUsage & BufferUsage::GPU_RENDER_TARGET) {
        bufferUsage |= BO_USE_RENDERING;
    }
    if (grallocUsage & BufferUsage::COMPOSER_OVERLAY) {
        /* HWC wants to use display hardware, but can defer to OpenGL. */
        bufferUsage |= BO_USE_SCANOUT | BO_USE_TEXTURE;
    }
    if (grallocUsage & BufferUsage::PROTECTED) {
        bufferUsage |= BO_USE_PROTECTED;
    }
    if (grallocUsage & BufferUsage::COMPOSER_CURSOR) {
        bufferUsage |= BO_USE_NONE;
    }
    if (grallocUsage & BufferUsage::VIDEO_ENCODER) {
        /*HACK: See b/30054495 */
        bufferUsage |= BO_USE_SW_READ_OFTEN;
        bufferUsage |= BO_USE_HW_VIDEO_ENCODER;
    }
    if (grallocUsage & BufferUsage::CAMERA_OUTPUT) {
        bufferUsage |= BO_USE_CAMERA_WRITE;
    }
    if (grallocUsage & BufferUsage::CAMERA_INPUT) {
        bufferUsage |= BO_USE_CAMERA_READ;
    }
    if (grallocUsage & BufferUsage::RENDERSCRIPT) {
        bufferUsage |= BO_USE_RENDERSCRIPT;
    }
    if (grallocUsage & BufferUsage::VIDEO_DECODER) {
        bufferUsage |= BO_USE_HW_VIDEO_DECODER;
    }

    if (grallocUsage & BufferUsage::COMPOSER_CLIENT_TARGET) {
        bufferUsage |= BO_USE_FRAMEBUFFER;
    }
    /* Below is NXP private usage bit*/
    if (grallocUsage & GRALLOC_USAGE_PRIVATE_0) {
        bufferUsage |= BO_USE_GPU_TILED_VIV;
    }
    if (grallocUsage & GRALLOC_USAGE_PRIVATE_1) {
        bufferUsage |= BO_USE_GPU_TS_VIV;
    }
    if (grallocUsage & GRALLOC_USAGE_PRIVATE_2) {
        bufferUsage |= BO_USE_PADDING_BUFFER;
    }

    *outBufferUsage = bufferUsage;
    return 0;
}

int convertToMemDescriptor(const BufferDescriptorInfo& descriptor,
                           struct gralloc_buffer_descriptor* outMemDescriptor) {
    outMemDescriptor->name = descriptor.name;
    outMemDescriptor->width = descriptor.width;
    outMemDescriptor->height = descriptor.height;
    outMemDescriptor->droid_format = static_cast<int32_t>(descriptor.format);
    outMemDescriptor->droid_usage = descriptor.usage;
    outMemDescriptor->reserved_region_size = descriptor.reservedSize;

    if (convertToDrmFormat(descriptor.format, &outMemDescriptor->drm_format)) {
        std::string pixelFormatString = getPixelFormatString(descriptor.format);
        ALOGE("%s Unsupported fomat %s", __func__, pixelFormatString.c_str());
        return -1;
    }
    if (convertToBufferUsage(descriptor.usage, &outMemDescriptor->use_flags)) {
        std::string usageString = getUsageString(descriptor.usage);
        ALOGE("%s Unsupported usage flags %s", __func__, usageString.c_str());
        return -1;
    }
    return 0;
}

int convertToMapUsage(uint64_t grallocUsage, uint32_t* outMapUsage) {
    uint32_t mapUsage = BO_MAP_NONE;

    if (grallocUsage & BufferUsage::CPU_READ_MASK) {
        mapUsage |= BO_MAP_READ;
    }
    if (grallocUsage & BufferUsage::CPU_WRITE_MASK) {
        mapUsage |= BO_MAP_WRITE;
    }

    *outMapUsage = mapUsage;
    return 0;
}

int convertToFenceFd(const hidl_handle& fenceHandle, int* outFenceFd) {
    if (!outFenceFd) {
        return -EINVAL;
    }

    const native_handle_t* nativeHandle = fenceHandle.getNativeHandle();
    if (nativeHandle && nativeHandle->numFds > 1) {
        return -EINVAL;
    }

    *outFenceFd = (nativeHandle && nativeHandle->numFds == 1) ? nativeHandle->data[0] : -1;
    return 0;
}

int convertToFenceHandle(int fenceFd, hidl_handle* outFenceHandle) {
    if (!outFenceHandle) {
        return -EINVAL;
    }
    if (fenceFd < 0) {
        return 0;
    }

    NATIVE_HANDLE_DECLARE_STORAGE(handleStorage, 1, 0);
    auto fenceHandle = native_handle_init(handleStorage, 1, 0);
    fenceHandle->data[0] = fenceFd;

    *outFenceHandle = fenceHandle;
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

                    {DRM_FORMAT_YVU420_ANDROID, // YV12
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

                    {DRM_FORMAT_RAW16,
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
