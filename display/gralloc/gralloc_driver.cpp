/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Copyright 2024 NXP.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gralloc_driver.h"

#include <aidl/android/hardware/graphics/common/BufferUsage.h>
#include <aidl/android/hardware/graphics/common/PixelFormat.h>
#include <cutils/log.h>
#include <fcntl.h>
#include <hardware/gralloc.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <xf86drm.h>

#include <cstdlib>

#include "dma_buf_heaps.h"
#include "driver_helpers.h"
#include "driver_utils.h"

#define GPU_MODULE_ID "gralloc_viv"
#define DRM_VIV_GEM_TILING_TILED 0x02

using aidl::android::hardware::graphics::common::BufferUsage;
using aidl::android::hardware::graphics::common::PixelFormat;

std::shared_ptr<gralloc_driver> gralloc_driver::get_instance() {
    static std::shared_ptr<gralloc_driver> s_instance = []() {
        return std::shared_ptr<gralloc_driver>(new gralloc_driver());
    }();

    if (!s_instance->is_initialized()) {
        ALOGE("Failed to initialize driver.");
        return nullptr;
    }

    return s_instance;
}

gralloc_driver::gralloc_driver() {
    hw_module_t **hwm = reinterpret_cast<hw_module_t **>(&mGPUModule);
    if (hw_get_module(GPU_MODULE_ID, const_cast<const hw_module_t **>(hwm)) == 0) {
        int status = gralloc_open((const hw_module_t *)mGPUModule, &mGPUAlloc);
        if (status || !mGPUAlloc) {
            ALOGI("no gpu gralloc device!");
        }
    }
}

gralloc_driver::~gralloc_driver() {
    if (mGPUAlloc != nullptr) {
        mGPUAlloc->common.close((struct hw_device_t *)mGPUAlloc);
    }
}

bool gralloc_driver::is_initialized() {
    return true;
}

bool gralloc_driver::is_supported(const struct gralloc_buffer_descriptor *descriptor) {
    /** bits 33-47 must be zero and are reserved for future versions */
    if (descriptor->usage & 0xFFFE00000000) {
        ALOGE("%s: usage(%" PRIx64 ") check failed", __func__, descriptor->usage);
        return false;
    } else {
        return true;
    }
}

int32_t gralloc_driver::create_reserved_region(uint64_t reserved_region_size) {
    int32_t reserved_region_fd = allocator_allocate_system_memory(reserved_region_size);
    if (reserved_region_fd < 0) {
        ALOGI("Failed to create reserved_region");
    }
    return reserved_region_fd;
}

int32_t gralloc_driver::dmabuf_allocate(gralloc_buffer_descriptor *desc,
                                        gralloc_handle **out_handle) {
    uint32_t aligned_width, aligned_height;
    if (!drv_pixel_width_height_alignment(desc, &aligned_width, &aligned_height)) {
        ALOGE("%s: get width/height alignment failed", __func__);
        return -1;
    }

    drv_buffer_info_calculation(desc, aligned_width, aligned_height);
    if (out_handle == nullptr)
        return 0;

    auto hnd = allocator_allocate(desc);
    if (hnd == nullptr) {
        ALOGE("%s: cannot allocate memory from dmabuf", __func__);
        return -1;
    }

    hnd->pixel_stride = aligned_width;
    *out_handle = hnd;
    return 0;
}

bool gralloc_driver::allocate_from_gpu_gralloc(int32_t pixel_format, uint64_t usage,
                                               uint64_t flags) {
    auto info = getPixleFormatInfo(pixel_format);
    if (!info || (mGPUAlloc == nullptr))
        return false;

    bool gpu_gralloc = false;
    if (info->is_rgb &&
        !(usage &
          (GRALLOC_USAGE_HW_FB | USAGE_PADDING_BUFFER | GRALLOC_USAGE_PROTECTED |
           GRALLOC_USAGE_HW_VIDEO_ENCODER)))
        gpu_gralloc = true;

    // The tiled framebuffer for imx8mq should allocate from GPU gralloc
    if ((usage & GRALLOC_USAGE_HW_FB) && (flags & NXP_GRALLOC_FLAGS_TILED_FRAMEBUFFER))
        gpu_gralloc = true;

    return gpu_gralloc;
}

int32_t gralloc_driver::allocate(gralloc_buffer_descriptor *desc, native_handle_t **out_handle) {
    int ret = 0;
    gralloc_handle *handle = nullptr;
    if (allocate_from_gpu_gralloc(desc->pixel_format, desc->usage, desc->flags)) {
        native_handle **phnd = reinterpret_cast<native_handle **>(&handle);
        ret = mGPUAlloc->alloc(mGPUAlloc, static_cast<int>(desc->width),
                               static_cast<int>(desc->height), desc->pixel_format,
                               static_cast<int>(desc->usage), const_cast<buffer_handle_t *>(phnd),
                               reinterpret_cast<int *>(&desc->pixel_stride));
        if (ret == 0 && handle != nullptr) {
            handle->usage |= (desc->usage & 0x100000000); // FRONT_BUFFER = 1L << 32
            handle->layer_count = desc->layer_count;
            handle->flags = desc->flags | NXP_GRALLOC_FLAGS_FROM_GPU;
            if ((handle->usage & GRALLOC_USAGE_HW_FB) ||
                (handle->usage & GRALLOC_USAGE_HW_COMPOSER)) {
                handle->flags |=
                        NXP_GRALLOC_FLAGS_CONTIGUOUS; // TODO: need to check with GPU galloc side
            }
            *out_handle = handle;
            dmabuf_allocate(desc, nullptr); // calculate buffer infomation, but not allocate memory
        }
    } else {
        ret = dmabuf_allocate(desc, &handle);
        if (ret == 0) {
            if ((handle->flags & NXP_GRALLOC_FLAGS_DISPLAY_UNDERRUN) &&
                (handle->usage & USAGE_GPU_TILED_VIV) && (handle->usage & GRALLOC_USAGE_HW_FB))
                handle->tiling = DRM_VIV_GEM_TILING_TILED;
            *out_handle = handle;
        }
    }
    if (ret != 0 || handle == nullptr) {
        ALOGE("%s alloc memory failed", __func__);
        return ret;
    }

    if (handle->flags & NXP_GRALLOC_FLAGS_CONTIGUOUS) {
        // Get physical address for the buffer with contiguous memory
        uint64_t phys = 0;
        if (allocator_get_physical_address(handle, &phys) == 0)
            handle->phys = phys;
        else
            ALOGE("%s: get physical address for contigious memory buffer failed!");
    }

    handle->drm_format = desc->drm_format;
    if ((desc->usage & USAGE_GPU_TS_VIV) && (desc->flags & NXP_GRALLOC_FLAGS_TILED_FRAMEBUFFER)) {
        handle->format_modifier = DRM_FORMAT_MOD_VIVANTE_SUPER_TILED_FC;
    } else if (desc->usage & USAGE_GPU_TILED_VIV) {
        handle->format_modifier = (desc->flags & NXP_GRALLOC_FLAGS_DISPLAY_UNDERRUN)
                ? DRM_FORMAT_MOD_VIVANTE_TILED
                : DRM_FORMAT_MOD_VIVANTE_SUPER_TILED;
    } else {
        handle->format_modifier = desc->modifier;
    }
    handle->num_planes = desc->num_planes;
    for (uint32_t i = 0; i < desc->num_planes; i++) {
        handle->strides[i] = desc->strides[i];
        handle->offsets[i] = desc->offsets[i];
        handle->sizes[i] = desc->sizes[i];
    }

    static std::atomic<uint32_t> next_buffer_id{1};
    handle->backing_store_id = next_buffer_id++;

    int32_t reserved_region_fd;
    uint32_t name_size;
    uint32_t num_fds = GRALLOC_HANDLE_NUM_FDS; // TODO: the default fds includes all fds of buffer
    handle->reserved_region_size = desc->reserved_region_size;
    if (desc->reserved_region_size > 0) {
        reserved_region_fd = create_reserved_region(desc->reserved_region_size);
        if (reserved_region_fd < 0) {
            return reserved_region_fd;
        }
        num_fds += 1;

        void *mapping = mmap(nullptr, handle->reserved_region_size, PROT_WRITE | PROT_READ,
                             MAP_SHARED, reserved_region_fd, 0);
        if (mapping == MAP_FAILED) {
            ALOGE("%s Failed to mmap reserved region: %s.", __func__, strerror(errno));
            return -errno;
        }
        handle->attr_base = (uint64_t)mapping;
    } else {
        reserved_region_fd = -1;
    }
    handle->numFds = static_cast<int>(num_fds);
    handle->numInts = static_cast<int>(
            ((sizeof(gralloc_handle) - sizeof(native_handle_t)) / sizeof(int)) - num_fds);
    handle->fds[num_fds - 1] = reserved_region_fd;

    if (desc->name.size() > BUFFER_NAME_MAX_SIZE - 1)
        name_size = BUFFER_NAME_MAX_SIZE;
    else
        name_size = static_cast<uint32_t>(desc->name.size()) + 1;
    snprintf(handle->name, name_size, "%s", desc->name.c_str());

    ALOGI("allocated %s buffer info: %d x %d, pixel_format=0x%" PRIx32 "(%s), drm_format=%s, "
          "modifer=0x%" PRIx64 ", usage=0x%" PRIx64 "(%s), flags=0x%" PRIx32 ", "
          "plane:{offset(0x%x, 0x%x, 0x%x), byte_stride(%d, %d, %d)}, size=%ld, "
          "name=%s",
          (handle->flags & NXP_GRALLOC_FLAGS_FROM_GPU) ? "GPU" : "DMA_BUF", desc->width,
          desc->height, desc->pixel_format, getPixelFormatString(desc->pixel_format).c_str(),
          getDrmFormatString(desc->drm_format).c_str(), handle->format_modifier, desc->usage,
          getUsageString(desc->usage).c_str(), handle->flags, desc->offsets[0], desc->offsets[1],
          desc->offsets[2], desc->strides[0], desc->strides[1], desc->strides[2], desc->total_size,
          desc->name.c_str());

    return 0;
}

int32_t gralloc_driver::retain(buffer_handle_t handle) {
    std::lock_guard<std::mutex> lock(mutex_);

    int ret = 0;
    auto hnd = gralloc_convert_handle(handle);
    if (!hnd) {
        ALOGE("%s Invalid handle.", __func__);
        return -EINVAL;
    }
    // virtual address should be cleared when import buffer handle
    gralloc_handle *hnd_write = const_cast<gralloc_handle *>(hnd);
    hnd_write->base = 0;
    hnd_write->attr_base = 0;
    hnd_write->lock_count = 0;
    hnd_write->cpu_write = 0;
    if (hnd->flags & NXP_GRALLOC_FLAGS_FROM_GPU) {
        ret = mGPUModule->registerBuffer(mGPUModule, handle);
    } else {
    }

    if (ret != 0)
        return -EINVAL;

    void *mapping = mmap(nullptr, hnd->reserved_region_size, PROT_WRITE | PROT_READ, MAP_SHARED,
                         hnd->fds[hnd->numFds - 1], 0);
    if (mapping == MAP_FAILED) {
        ALOGE("%s Failed to mmap reserved region: %s.", __func__, strerror(errno));
        return -errno;
    }
    hnd_write->attr_base = (uint64_t)mapping;

    return 0;
}

int32_t gralloc_driver::release(buffer_handle_t handle) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto hnd = gralloc_convert_handle(handle);
    if (!hnd) {
        ALOGE("%s Invalid handle.", __func__);
        return -EINVAL;
    }

    if (hnd->attr_base != 0) {
        munmap(reinterpret_cast<void *>(hnd->attr_base), hnd->reserved_region_size);
    }

    if (hnd->flags & NXP_GRALLOC_FLAGS_FROM_GPU) {
        if (hnd->fds[hnd->numFds - 1] > 0) {
            int ret = close(hnd->fds[hnd->numFds - 1]);
            if (ret != 0)
                ALOGE("%s: close reserved region fd failed(%s)", __func__, strerror(errno));
        }
        return mGPUAlloc->free(mGPUAlloc, handle);
    } else {
        if (hnd->base != 0) {
            allocator_unmap(hnd);
        }
        native_handle_close(hnd);
        native_handle_delete(static_cast<native_handle_t *>(const_cast<gralloc_handle *>(hnd)));
    }

    return 0;
}

int32_t gralloc_driver::lock(buffer_handle_t handle, int32_t acquire_fence,
                             bool close_acquire_fence, const struct rectangle *rect, uint64_t usage,
                             uint8_t *addr[DRV_MAX_PLANES]) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto hnd = gralloc_convert_handle(handle);
    if (!hnd) {
        ALOGE("%s Invalid handle.", __func__);
        return -EINVAL;
    }
    if (hnd->cpu_write != 0 && (usage & GRALLOC_USAGE_SW_WRITE_MASK)) {
        ALOGW("%s: attemp to call lock() for writing an already locked buffer(%p)", __func__,
              handle);
        const_cast<gralloc_handle *>(hnd)->lock_count++;
        return 0; // regard as lock successfully
    }

    int32_t ret = gralloc_sync_wait(acquire_fence, close_acquire_fence);
    if (ret) {
        ALOGE("%s gralloc sync wait failed", __func__);
        return ret;
    }

    void *vaddr = nullptr;
    if (hnd->flags & NXP_GRALLOC_FLAGS_FROM_GPU) {
        ret = mGPUModule->lock(mGPUModule, handle, static_cast<int>(usage),
                               static_cast<int>(rect->x), static_cast<int>(rect->y),
                               static_cast<int>(rect->width), static_cast<int>(rect->height),
                               &vaddr);
    } else if (hnd->fds[0] >= 0) { // TODO: Need check if includes CPU R/W usage
        if (!hnd->base && (allocator_map(hnd) != 0)) {
            ALOGE("%s: buffer:%s cannot mmap %s", __func__, hnd->name, strerror(errno));
            ret = -EINVAL;
        } else {
            vaddr = reinterpret_cast<void *>(hnd->base);
        }
    }
    if (ret != 0) {
        ALOGE("%s lock memory failed", __func__);
        return ret;
    }

    const_cast<gralloc_handle *>(hnd)->lock_count++;
    const_cast<gralloc_handle *>(hnd)->cpu_write =
            (usage & GRALLOC_USAGE_SW_WRITE_MASK) ? true : false;
    if ((usage & (GRALLOC_USAGE_SW_READ_MASK | GRALLOC_USAGE_SW_WRITE_MASK)) != 0)
        allocator_sync_start(hnd, usage & GRALLOC_USAGE_SW_READ_MASK,
                             usage & GRALLOC_USAGE_SW_WRITE_MASK);

    addr[0] = (uint8_t *)vaddr;
    return 0;
}

int32_t gralloc_driver::unlock(buffer_handle_t handle, int32_t *release_fence) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto hnd = gralloc_convert_handle(handle);
    if (!hnd) {
        ALOGE("%s Invalid handle.", __func__);
        return -EINVAL;
    }

    if (hnd->lock_count == 0) {
        ALOGE("%s: cannot unlock() an unlocked buffer(%p)", __func__, handle);
        return -EINVAL;
    }

    int ret = 0;
    if (hnd->flags & NXP_GRALLOC_FLAGS_FROM_GPU) {
        ret = mGPUModule->unlock(mGPUModule, handle);
    } else if (hnd->cpu_write) {
        ret = allocator_sync_end(hnd, false, true);
    }

    if (ret != 0) {
        ALOGE("%s unlock memory failed", __func__);
        return ret;
    }

    gralloc_handle *hnd_write = const_cast<gralloc_handle *>(hnd);
    hnd->lock_count > 0 ? --hnd_write->lock_count : hnd_write->lock_count = 0;
    if (hnd_write->lock_count == 0)
        hnd_write->cpu_write = 0;

    *release_fence = -1;
    return 0;
}

int32_t gralloc_driver::invalidate(buffer_handle_t handle) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto hnd = gralloc_convert_handle(handle);
    if (!hnd) {
        ALOGE("%s Invalid handle.", __func__);
        return -EINVAL;
    }

    if (hnd->lock_count == 0) {
        ALOGE("%s: cannot invalidate() an unlocked buffer(%p)", __func__, handle);
        return -EINVAL;
    }

    if (hnd->flags & NXP_GRALLOC_FLAGS_FROM_GPU) {
    } else {
        allocator_sync_start(hnd, true, false);
    }

    return 0;
}

int32_t gralloc_driver::flush(buffer_handle_t handle) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto hnd = gralloc_convert_handle(handle);
    if (!hnd) {
        ALOGE("%s Invalid handle.", __func__);
        return -EINVAL;
    }

    if (hnd->lock_count == 0) {
        ALOGE("%s: cannot flush() an unlocked buffer(%p)", __func__, handle);
        return -EINVAL;
    }

    if (hnd->flags & NXP_GRALLOC_FLAGS_FROM_GPU) {
    } else {
        allocator_sync_end(hnd, false, true);
    }

    return 0;
}

int32_t gralloc_driver::get_reserved_region(buffer_handle_t handle, void **reserved_region_addr,
                                            uint64_t *reserved_region_size) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto hnd = gralloc_convert_handle(handle);
    if (!hnd) {
        ALOGE("%s Invalid handle.", __func__);
        return -EINVAL;
    }

    if (hnd->fds[hnd->numFds - 1] <= 0) {
        ALOGE("%s Buffer does not have reserved region.", __func__);
        return -EINVAL;
    }

    *reserved_region_addr = reinterpret_cast<void *>(hnd->attr_base);
    *reserved_region_size = hnd->reserved_region_size;
    return 0;
}
