/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Copyright 2024 NXP.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GRALLOC_HANDLE_H
#define GRALLOC_HANDLE_H

#include <cutils/native_handle.h>

#include <cstdint>
#include <string>

#include "../../include/graphics_ext.h"

#define DRV_MAX_PLANES 4
#define DRV_MAX_FDS (DRV_MAX_PLANES + 1)
#define BUFFER_NAME_MAX_SIZE 64

#define GRALLOC_HANDLE_NUM_FDS 1
#define GRALLOC_HANDLE_NUM_INTS \
    ((sizeof(gralloc_handle) - sizeof(native_handle_t)) / sizeof(int) - GRALLOC_HANDLE_NUM_FDS)

#define ALIGN_PIXEL_2(x) ((x + 1) & ~1U)
#define ALIGN_PIXEL_4(x) ((x + 3) & ~3U)
#define ALIGN_PIXEL_8(x) ((x + 7) & ~7U)
#define ALIGN_PIXEL_16(x) ((x + 15) & ~15U)
#define ALIGN_PIXEL_32(x) ((x + 31) & ~31U)
#define ALIGN_PIXEL_64(x) ((x + 63) & ~63U)
#define ALIGN_PIXEL_256(x) ((x + 255) & ~255U)

#define NXP_GRALLOC_FLAGS_FRAMEBUFFER (1 << 0)
#define NXP_GRALLOC_FLAGS_FROM_GPU (1 << 1)
#define NXP_GRALLOC_FLAGS_CONTIGUOUS (1 << 2)
#define NXP_GRALLOC_FLAGS_CACHED (1 << 3)

#define NXP_GRALLOC_FLAGS_TILED_FRAMEBUFFER (1 << 16)
#define NXP_GRALLOC_FLAGS_DISPLAY_UNDERRUN (1 << 17)

enum {
    USAGE_GPU_TILED_VIV = 0x10000000, // GRALLOC_USAGE_PRIVATE_0
    USAGE_GPU_TS_VIV = 0x20000000,    // GRALLOC_USAGE_PRIVATE_1

    /* buffer size of hantro decoder is not to yuv pixel size, it need to
     * pad some bytes for vpu usage, so add this flag */
    USAGE_PADDING_BUFFER = 0x40000000, // GRALLOC_USAGE_PRIVATE_2
};

struct gralloc_handle : public native_handle {
    static const int sMagic = 0x3141592;
    int32_t fds[DRV_MAX_FDS]; /* The fds[numFds-1] is the fd for reserved region memory */
    uint32_t magic;
    uint32_t width;
    uint32_t height;
    int32_t format; /* Android request format, pixel format */
    uint32_t drm_format{};
    uint64_t format_modifier{};
    uint64_t usage;
    uint32_t num_planes{};
    uint32_t strides[DRV_MAX_PLANES]; /* stride in bytes */
    uint32_t offsets[DRV_MAX_PLANES];
    uint32_t sizes[DRV_MAX_PLANES];
    uint32_t pixel_stride; /* stride in pixels. used for validation */
    uint32_t layer_count;
    uint64_t reserved_region_size{};
    uint64_t total_size; /* total allocation size, not include reserved region */
    uint64_t backing_store_id{};
    uint64_t nxp_reserved[6];
    char name[BUFFER_NAME_MAX_SIZE];
    int32_t pid{};    /* owner of data (for validation) */
    uint32_t flags{}; /* 0x0001:PRIV_FLAGS_FRAMEBUFFER */
    uint32_t lock_count{};
    uint32_t cpu_write{};
    uint64_t base{};
    uint64_t attr_base{};
    uint64_t phys{};
    uint64_t surface{}; /* gpu-viv use only */
    uint64_t tiling{};
    uint64_t data{}; /* gpu-viv use only */
    uint64_t viv_reserved[5];

    gralloc_handle(int in_shared_fd, uint64_t in_size, uint64_t in_usage, int32_t in_pixel_format,
                   uint32_t in_width, uint32_t in_height, uint32_t in_layer_count,
                   uint32_t in_stride)
          : fds{in_shared_fd},
            width{in_width},
            height{in_height},
            format{in_pixel_format},
            usage{in_usage},
            pixel_stride{in_stride},
            layer_count{in_layer_count},
            total_size{in_size} {
        version = sizeof(native_handle);
        numFds = GRALLOC_HANDLE_NUM_FDS;
        numInts = GRALLOC_HANDLE_NUM_INTS;
        magic = sMagic;
    }
};
typedef const struct gralloc_handle *gralloc_handle_t;

struct gralloc_buffer_descriptor {
    uint32_t width;
    uint32_t height;
    int32_t pixel_format;
    uint32_t layer_count;
    uint64_t usage;
    uint64_t reserved_region_size;
    std::string name;

    uint32_t drm_format;
    uint64_t modifier;
    uint32_t flags;
    uint64_t total_size;
    uint32_t pixel_stride;
    uint32_t num_planes;
    uint32_t strides[DRV_MAX_PLANES]; /* stride in bytes */
    uint32_t offsets[DRV_MAX_PLANES];
    uint32_t sizes[DRV_MAX_PLANES];
};

inline int32_t gralloc_handle_fd(buffer_handle_t handle) {
    return static_cast<gralloc_handle_t>(handle)->fds[0];
}
inline uint32_t gralloc_handle_width(buffer_handle_t handle) {
    return static_cast<gralloc_handle_t>(handle)->width;
}
inline uint32_t gralloc_handle_height(buffer_handle_t handle) {
    return static_cast<gralloc_handle_t>(handle)->height;
}
inline int32_t gralloc_handle_format(buffer_handle_t handle) {
    return static_cast<gralloc_handle_t>(handle)->format;
}
inline uint32_t gralloc_handle_pixel_stride(buffer_handle_t handle) {
    return static_cast<gralloc_handle_t>(handle)->pixel_stride;
}
inline uint64_t gralloc_handle_phys(buffer_handle_t handle) {
    return static_cast<gralloc_handle_t>(handle)->phys;
}
inline uint64_t gralloc_handle_base(buffer_handle_t handle) {
    return static_cast<gralloc_handle_t>(handle)->base;
}
inline uint64_t gralloc_handle_size(buffer_handle_t handle) {
    return static_cast<gralloc_handle_t>(handle)->total_size;
}

inline void gralloc_handle_set_fd(buffer_handle_t handle, int32_t fd) {
    static_cast<gralloc_handle *>(const_cast<native_handle_t *>(handle))->fds[0] = fd;
}
inline void gralloc_handle_set_width(buffer_handle_t handle, uint32_t width) {
    static_cast<gralloc_handle *>(const_cast<native_handle_t *>(handle))->width = width;
}
inline void gralloc_handle_set_height(buffer_handle_t handle, uint32_t height) {
    static_cast<gralloc_handle *>(const_cast<native_handle_t *>(handle))->height = height;
}
inline void gralloc_handle_set_size(buffer_handle_t handle, uint64_t size) {
    static_cast<gralloc_handle *>(const_cast<native_handle_t *>(handle))->total_size = size;
}
inline void gralloc_handle_set_pixel_stride(buffer_handle_t handle, uint32_t stride) {
    static_cast<gralloc_handle *>(const_cast<native_handle_t *>(handle))->pixel_stride = stride;
}
inline void gralloc_handle_set_phys(buffer_handle_t handle, uint64_t phys) {
    static_cast<gralloc_handle *>(const_cast<native_handle_t *>(handle))->phys = phys;
}
inline void gralloc_handle_set_base(buffer_handle_t handle, uint64_t base) {
    static_cast<gralloc_handle *>(const_cast<native_handle_t *>(handle))->base = base;
}

#endif
