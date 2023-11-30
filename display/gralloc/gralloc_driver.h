/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Copyright 2023 NXP.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __GRALLOC_DRIVER_H
#define __GRALLOC_DRIVER_H

#include <MemoryManager.h>

#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "gralloc_helpers.h"

class gralloc_driver {
public:
    static gralloc_driver *get_instance();
    bool is_supported(const struct gralloc_buffer_descriptor *descriptor);
    int32_t allocate(const struct gralloc_buffer_descriptor *descriptor,
                     native_handle_t **out_handle);

    int32_t retain(buffer_handle_t handle);
    int32_t release(buffer_handle_t handle);

    int32_t lock(buffer_handle_t handle, int32_t acquire_fence, bool close_acquire_fence,
                 const struct rectangle *rect, uint32_t map_flags, uint8_t *addr[DRV_MAX_PLANES]);
    int32_t unlock(buffer_handle_t handle, int32_t *release_fence);

    int32_t invalidate(buffer_handle_t handle);
    int32_t flush(buffer_handle_t handle);

    int32_t get_backing_store(buffer_handle_t handle, uint64_t *out_store);
    int32_t validate_buffer(const struct gralloc_buffer_descriptor *descriptor,
                            buffer_handle_t handle);
    int32_t create_reserved_region(uint64_t reserved_region_size);
    int32_t get_reserved_region(buffer_handle_t handle, void **reserved_region_addr,
                                uint64_t *reserved_region_size);

    uint32_t get_resolved_drm_format(uint32_t drm_format, uint64_t usage);

    uint32_t get_id(gralloc_handle_t hnd) const { return hnd->id; }

    uint32_t get_width(gralloc_handle_t hnd) const { return hnd->width; }

    uint32_t get_pixel_stride(gralloc_handle_t hnd) const { return hnd->stride; }

    uint32_t get_height(gralloc_handle_t hnd) const { return hnd->height; }

    uint32_t get_format(gralloc_handle_t hnd) const { return hnd->fslFormat; }

    uint64_t get_format_modifier(gralloc_handle_t hnd) const { return hnd->format_modifier; }

    uint64_t get_total_size(gralloc_handle_t hnd) const { return hnd->total_size; }

    uint32_t get_num_planes(gralloc_handle_t hnd) const { return hnd->num_planes; }

    uint32_t get_plane_offset(gralloc_handle_t hnd, uint32_t plane) const {
        return hnd->offsets[plane];
    }

    uint32_t get_plane_stride(gralloc_handle_t hnd, uint32_t plane) const {
        return hnd->strides[plane];
    }

    uint32_t get_plane_size(gralloc_handle_t hnd, uint32_t plane) const {
        return hnd->sizes[plane];
    }

    int32_t get_android_format(gralloc_handle_t hnd) const { return hnd->format; }

    uint64_t get_android_usage(gralloc_handle_t hnd) const {
        return static_cast<uint64_t>(hnd->usage);
    }

private:
    gralloc_driver();
    ~gralloc_driver();

    bool is_initialized();
    gralloc_driver(gralloc_driver const &);
    gralloc_driver operator=(gralloc_driver const &);

    fsl::MemoryManager *pManager;
    std::mutex mutex_;
};

#endif
