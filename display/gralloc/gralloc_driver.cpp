/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Copyright 2023 NXP.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#undef LOG_TAG
#define LOG_TAG "gralloc_driver"

#include "gralloc_driver.h"

#include <fcntl.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <xf86drm.h>

#include <cstdlib>

// #include <android/hardware/graphics/common/1.2/types.h>
#include <DisplayUtil.h>
#include <hardware/gralloc1.h>

#include "../../include/graphics_ext.h"
#include "helpers.h"

// using android::hardware::graphics::common::V1_2::BufferUsage;
using namespace fsl;
std::unordered_map<gralloc_handle_t, void *> reserved_region_addrs;
pthread_mutex_t reserved_region_addrs_lock = PTHREAD_MUTEX_INITIALIZER;

gralloc_driver *gralloc_driver::get_instance() {
    static gralloc_driver s_instance;
    if (!s_instance.is_initialized()) {
        ALOGE("Failed to initialize driver.");
        return nullptr;
    }

    return &s_instance;
}

gralloc_driver::gralloc_driver() : pManager(nullptr) {
    pManager = MemoryManager::getInstance();
    if (pManager == nullptr) {
        ALOGE("%s can't get memory manager", __func__);
    }
}

gralloc_driver::~gralloc_driver() {}

bool gralloc_driver::is_initialized() {
    return pManager != nullptr;
}

bool gralloc_driver::is_supported(const struct gralloc_buffer_descriptor *descriptor) {
    ALOGI("%s check descriptor name=%s, width=%d, height=%d, droid_format=%s, usage=%s", __func__,
          descriptor->name.c_str(), descriptor->width, descriptor->height,
          getGrallocFormatString(descriptor->droid_format).c_str(),
          getUsageString(descriptor->droid_usage).c_str());
    if (descriptor->droid_usage &
        0xFFFE00000000) /** bits 33-47 must be zero and are reserved for future versions */
        return false;
    else
        return true;
}

int32_t gralloc_driver::create_reserved_region(uint64_t reserved_region_size) {
    int32_t reserved_region_fd = pManager->allocSystemMemeory(reserved_region_size);
    if (reserved_region_fd < 0) {
        ALOGI("Failed to create reserved_region");
    }
    return reserved_region_fd;
}

int32_t gralloc_driver::allocate(const struct gralloc_buffer_descriptor *descriptor,
                                 native_handle_t **out_handle) {
    MemoryDesc desc;
    Memory *hnd = NULL;
    int32_t reserved_region_fd;
    int name_size;
    int flags = 0;
    uint64_t usage;

    desc.mWidth = descriptor->width;
    desc.mHeight = descriptor->height;
    desc.mFormat = convert_pixel_format_to_gralloc_format(descriptor->droid_format);
    desc.mFslFormat = convert_gralloc_format_to_nxp_format(desc.mFormat);

    usage = static_cast<uint64_t>(descriptor->droid_usage);
    if (descriptor->use_flags & BO_USE_FRAMEBUFFER) {
        flags |= FLAGS_FRAMEBUFFER;
        usage |= GRALLOC1_CONSUMER_USAGE_HWCOMPOSER | GRALLOC1_PRODUCER_USAGE_GPU_RENDER_TARGET;
    }
    if ((descriptor->use_flags & BO_USE_SW_READ_OFTEN) != 0 ||
        (descriptor->use_flags & BO_USE_SW_WRITE_OFTEN) != 0) {
        flags |= FLAGS_CPU;
    }

    desc.mProduceUsage = usage; // convert_buffer_usage_to_nxp_usage(descriptor->droid_usage);
    desc.mFlag = flags;         // convert_bo_use_flages_to_nxp_flags(descriptor->use_flags);
    desc.checkFormat();

    int ret = pManager->allocMemory(desc, &hnd);
    if (ret != 0) {
        ALOGE("%s alloc memory failed", __func__);
        return ret;
    }
    static std::atomic<uint32_t> next_buffer_id{1};
    int num_fds = 2; // TODO: the default fds include hnd->fd and hnd->fd_meta, here hardcode it
    hnd->id = next_buffer_id++;
    if (descriptor->reserved_region_size > 0) {
        reserved_region_fd = create_reserved_region(descriptor->reserved_region_size);
        if (reserved_region_fd < 0) {
            return reserved_region_fd;
        }
        num_fds += 1;
    } else {
        reserved_region_fd = -1;
    }
    hnd->numFds = num_fds;
    hnd->numInts = ((sizeof(Memory) - sizeof(native_handle_t)) / sizeof(int)) - num_fds;

    hnd->fd_region = reserved_region_fd;
    hnd->reserved_region_size = descriptor->reserved_region_size;
    hnd->total_size = hnd->size + hnd->reserved_region_size;
    if (descriptor->name.size() > BUFFER_NAME_MAX_SIZE - 1)
        name_size = BUFFER_NAME_MAX_SIZE;
    else
        name_size = descriptor->name.size() + 1;

    snprintf(hnd->name, name_size, "%s", descriptor->name.c_str());

    hnd->num_planes = drv_num_planes_from_format(hnd->fslFormat);

    uint32_t stride = 0;
    stride = drv_stride_from_format(hnd->fslFormat, hnd->stride /*aligned_width*/, 0);

    /* Calculate size and assign stride, size, offset to each plane based on format */
    drv_bo_from_format(hnd, stride, hnd->height, hnd->fslFormat);

    *out_handle = hnd;
    return 0;
}

int32_t gralloc_driver::retain(buffer_handle_t handle) {
    int ret;
    std::lock_guard<std::mutex> lock(mutex_);

    auto hnd = gralloc_convert_handle(handle);
    if (!hnd) {
        ALOGE("%s Invalid handle.", __func__);
        return -EINVAL;
    }
    // virtual address should be cleared when import buffer handle
    const_cast<gralloc_handle *>(hnd)->base = 0;

    ret = pManager->retainMemory(const_cast<gralloc_handle *>(hnd));
    if (ret != 0) {
        ALOGE("%s retain memory failed", __func__);
        return -EINVAL;
    }

    return 0;
}

int32_t gralloc_driver::release(buffer_handle_t handle) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto hnd = gralloc_convert_handle(handle);
    if (!hnd) {
        ALOGE("%s Invalid handle.", __func__);
        return -EINVAL;
    }

    if (reserved_region_addrs.count(hnd)) {
        munmap(reserved_region_addrs[hnd], hnd->reserved_region_size);
        pthread_mutex_lock(&reserved_region_addrs_lock);
        reserved_region_addrs.erase(hnd);
        pthread_mutex_unlock(&reserved_region_addrs_lock);
    }

    pManager->releaseMemory(const_cast<gralloc_handle *>(hnd));

    return 0;
}

int32_t gralloc_driver::lock(buffer_handle_t handle, int32_t acquire_fence,
                             bool close_acquire_fence, const struct rectangle *rect,
                             uint32_t map_flags, uint8_t *addr[DRV_MAX_PLANES]) {
    int32_t ret = gralloc_sync_wait(acquire_fence, close_acquire_fence);
    if (ret) {
        ALOGE("%s gralloc sync wait failed", __func__);
        return ret;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto hnd = gralloc_convert_handle(handle);
    if (!hnd) {
        ALOGE("%s Invalid handle.", __func__);
        return -EINVAL;
    }

    void *vaddr = nullptr;
    ret = pManager->lock(const_cast<gralloc_handle *>(hnd), hnd->usage, 0, 0, hnd->width,
                         hnd->height, &vaddr);
    if (ret != 0) {
        ALOGE("%s lock memory failed", __func__);
        return -EINVAL;
    }
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

    int ret = pManager->unlock(const_cast<gralloc_handle *>(hnd));
    if (ret != 0) {
        ALOGE("%s unlock memory failed", __func__);
        return -EINVAL;
    }

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

    return 0;
}

int32_t gralloc_driver::flush(buffer_handle_t handle) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto hnd = gralloc_convert_handle(handle);
    if (!hnd) {
        ALOGE("%s Invalid handle.", __func__);
        return -EINVAL;
    }

    int ret = pManager->flush(const_cast<gralloc_handle *>(hnd));
    if (ret != 0) {
        ALOGE("%s unlock memory failed", __func__);
        return -EINVAL;
    }

    return 0;
}

int32_t gralloc_driver::validate_buffer(const struct gralloc_buffer_descriptor *descriptor,
                                        buffer_handle_t handle) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto hnd = gralloc_convert_handle(handle);
    if (!hnd) {
        ALOGE("%s Invalid handle.", __func__);
        return -EINVAL;
    }

    MemoryDesc desc;
    uint64_t usage = static_cast<uint64_t>(descriptor->droid_usage);

    desc.mWidth = descriptor->width;
    desc.mHeight = descriptor->height;
    desc.mFormat = convert_pixel_format_to_gralloc_format(descriptor->droid_format);
    desc.mFslFormat = convert_gralloc_format_to_nxp_format(desc.mFormat);

    desc.mProduceUsage = usage;
    if (hnd->usage & USAGE_HW_VIDEO_ENCODER) {
        desc.mProduceUsage |= USAGE_HW_VIDEO_ENCODER;
    }
    desc.checkFormat();

    int ret = 0;
    ret = pManager->validateMemory(desc, const_cast<gralloc_handle *>(hnd));
    if (ret != 0) {
        ALOGE("%s failed, ret:%d", __func__, ret);
    }

    return ret;
}

int32_t gralloc_driver::get_reserved_region(buffer_handle_t handle, void **reserved_region_addr,
                                            uint64_t *reserved_region_size) {
    std::lock_guard<std::mutex> lock(mutex_);
    void *reserved_region_addr_;

    auto hnd = gralloc_convert_handle(handle);
    if (!hnd) {
        ALOGE("%s Invalid handle.", __func__);
        return -EINVAL;
    }

    if (hnd->fd_region <= 0) {
        ALOGE("%s Buffer does not have reserved region.", __func__);
        return -EINVAL;
    }

    if (reserved_region_addrs.count(hnd)) {
        reserved_region_addr_ = reserved_region_addrs[hnd];
    } else {
        reserved_region_addr_ = mmap(nullptr, hnd->reserved_region_size, PROT_WRITE | PROT_READ,
                                     MAP_SHARED, hnd->fd_region, 0);
        if (reserved_region_addr_ == MAP_FAILED) {
            ALOGE("%s Failed to mmap reserved region: %s.", __func__, strerror(errno));
            return -errno;
        }
        pthread_mutex_lock(&reserved_region_addrs_lock);
        reserved_region_addrs.emplace(hnd, reserved_region_addr_);
        pthread_mutex_unlock(&reserved_region_addrs_lock);
    }

    *reserved_region_addr = reserved_region_addr_;
    *reserved_region_size = hnd->reserved_region_size;
    return 0;
}

uint32_t gralloc_driver::get_resolved_drm_format(uint32_t drm_format, uint64_t usage) {
    return drv_resolve_format(drm_format, usage);
}
