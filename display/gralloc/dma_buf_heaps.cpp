/*
 * Copyright 2024 NXP
 * Copyright (C) 2022-2023 Arm Limited. All rights reserved.
 *
 * Copyright (C) 2008 The Android Open Source Project
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

#include "dma_buf_heaps.h"

#include <BufferAllocator/BufferAllocator.h>
#include <android-base/unique_fd.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <fcntl.h>
#include <hardware/gralloc.h>
#include <inttypes.h>
#include <linux/dma-buf-imx.h>
#include <sys/mman.h>

#include <algorithm>
#include <vector>

#include "driver_utils.h"
#include "gralloc_handle.h"

enum class dma_buf_heap {
    /* Upstream heaps */
    system,
    system_uncached,

    /* Custom heaps */
    physically_contiguous,
    physically_contiguous_uncached,
    protected_memory,
};

struct custom_heap {
    const char *name;
    struct {
        const char *name;
        int flags;
    } ion_fallback;
};

const custom_heap physically_contiguous_heap = {
        "reserved",
        {
                "reserved",
                0,
        },
};

const custom_heap physically_contiguous_uncached_heap = {
        "reserved-uncached",
        {
                "reserved-uncached",
                0,
        },
};

const custom_heap protected_memory_heap = {
        "secure",
        {
                "secure",
                0,
        },
};

const custom_heap custom_heaps[] = {
        physically_contiguous_heap,
        physically_contiguous_uncached_heap,
        protected_memory_heap,
};

static const char *get_dma_buf_heap_name(dma_buf_heap heap) {
    switch (heap) {
        case dma_buf_heap::system:
            return kDmabufSystemHeapName;
        case dma_buf_heap::system_uncached:
            return kDmabufSystemUncachedHeapName;
        case dma_buf_heap::physically_contiguous:
            return physically_contiguous_heap.name;
        case dma_buf_heap::physically_contiguous_uncached:
            return physically_contiguous_uncached_heap.name;
        case dma_buf_heap::protected_memory:
            return protected_memory_heap.name;
    }
}

static BufferAllocator *get_global_buffer_allocator() {
    static struct allocator_initialization {
        BufferAllocator allocator;
        allocator_initialization() {
            for (const auto &heap : custom_heaps) {
                allocator.MapNameToIonHeap(heap.name, heap.ion_fallback.name,
                                           heap.ion_fallback.flags);
            }
        }
    } instance;

    return &instance.allocator;
}

static dma_buf_heap pick_dma_buf_heap(uint64_t usage) {
    if (usage & GRALLOC_USAGE_PROTECTED) {
        return dma_buf_heap::protected_memory;
    } else if ((usage & GRALLOC_USAGE_HW_VIDEO_ENCODER) || (usage & GRALLOC_USAGE_HW_FB) ||
               (usage & GRALLOC_USAGE_HW_COMPOSER) || (usage & GRALLOC_USAGE_PRIVATE_3) ||
               (usage & GRALLOC_USAGE_HW_CAMERA_WRITE)) {
        if (usage & (GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN))
            return dma_buf_heap::physically_contiguous;
        else
            return dma_buf_heap::physically_contiguous_uncached;
    } else if ((usage & GRALLOC_USAGE_SW_READ_MASK) == GRALLOC_USAGE_SW_READ_OFTEN) {
        return dma_buf_heap::system;
    } else {
        return dma_buf_heap::system_uncached;
    }
}

int allocator_allocate_system_memory(uint64_t size) {
    auto allocator = get_global_buffer_allocator();

    int fd = allocator->Alloc(kDmabufSystemHeapName, size);
    if (fd < 0) {
        ALOGE("%s: libdmabufheap allocation failed for %s heap", __func__, kDmabufSystemHeapName);
    }

    return fd;
}

gralloc_handle *make_gralloc_handle(int shared_fd, uint64_t size, uint64_t usage,
                                    int32_t pixel_format, uint32_t width, uint32_t height,
                                    uint32_t layer_count, uint32_t stride) {
    void *mem = native_handle_create(GRALLOC_HANDLE_NUM_FDS, GRALLOC_HANDLE_NUM_INTS);
    if (mem == nullptr) {
        ALOGE("%s: gralloc_handle allocation failed", __func__);
        return nullptr;
    }

    return (gralloc_handle *)(new (mem) gralloc_handle(shared_fd, size, usage, pixel_format, width,
                                                       height, layer_count, stride));
}

gralloc_handle *allocator_allocate(const gralloc_buffer_descriptor *descriptor) {
    auto allocator = get_global_buffer_allocator();

    auto heap = pick_dma_buf_heap(descriptor->usage);
    auto heap_name = get_dma_buf_heap_name(heap);
    int fd = allocator->Alloc(heap_name, descriptor->total_size);
    if (fd < 0) {
        ALOGE("%s: libdmabufheap allocation failed for %s heap", __func__, heap_name);
        return nullptr;
    }

    gralloc_handle *hnd =
            make_gralloc_handle(fd, descriptor->total_size, descriptor->usage,
                                descriptor->pixel_format, descriptor->width, descriptor->height,
                                descriptor->layer_count, descriptor->pixel_stride);

    hnd->flags = descriptor->flags;
    if (heap != dma_buf_heap::system && heap != dma_buf_heap::system_uncached)
        hnd->flags |= NXP_GRALLOC_FLAGS_CONTIGIOUS;
    if (heap != dma_buf_heap::system_uncached &&
        heap != dma_buf_heap::physically_contiguous_uncached)
        hnd->flags |= NXP_GRALLOC_FLAGS_CACHED;

    return hnd;
}

static SyncType make_sync_type(bool read, bool write) {
    if (read && write) {
        return kSyncReadWrite;
    } else if (read) {
        return kSyncRead;
    } else if (write) {
        return kSyncWrite;
    } else {
        return static_cast<SyncType>(0);
    }
}

int allocator_sync_start(gralloc_handle_t handle, bool read, bool write) {
    auto allocator = get_global_buffer_allocator();
    return allocator->CpuSyncStart(static_cast<unsigned>(handle->fds[0]),
                                   make_sync_type(read, write));
}

int allocator_sync_end(gralloc_handle_t handle, bool read, bool write) {
    auto allocator = get_global_buffer_allocator();
    return allocator->CpuSyncEnd(static_cast<unsigned>(handle->fds[0]),
                                 make_sync_type(read, write));
}

int allocator_map(gralloc_handle_t handle) {
    void *hint = nullptr;
    int protection = PROT_READ | PROT_WRITE, flags = MAP_SHARED;
    off_t page_offset = 0;
    void *mapping = mmap(hint, handle->total_size, protection, flags, handle->fds[0], page_offset);
    if (MAP_FAILED == mapping) {
        ALOGE("%s: mmap(fds[0] = %d) failed: %s", __func__, handle->fds[0], strerror(errno));
        return -errno;
    }

    const_cast<gralloc_handle *>(handle)->base = reinterpret_cast<uint64_t>(mapping);

    return 0;
}

void allocator_unmap(gralloc_handle_t handle) {
    void *base = reinterpret_cast<void *>(handle->base);
    if (munmap(base, handle->total_size) < 0) {
        ALOGE("%s: munmap(base = %p, size = %" PRIu64 ") failed: %s", __func__, base,
              handle->total_size, strerror(errno));
    }

    gralloc_handle *hnd_write = const_cast<gralloc_handle *>(handle);
    hnd_write->base = 0;
    hnd_write->cpu_write = 0;
    hnd_write->lock_count = 0;
}

static bool allocator_has_protected_heap(uint64_t usage) {
    auto allocator = get_global_buffer_allocator();
    auto protected_heap = pick_dma_buf_heap(usage);
    auto protected_heap_name = get_dma_buf_heap_name(protected_heap);
    auto heap_list = allocator->GetDmabufHeapList();
    return std::find(heap_list.begin(), heap_list.end(), std::string(protected_heap_name)) !=
            heap_list.end();
}

bool allocator_supports_protected_memory(uint64_t usage) {
    static auto protected_heap_supported = allocator_has_protected_heap(usage);
    return protected_heap_supported;
}

void allocator_close() {
    /* nop */
}

int allocator_get_physical_address(int fd, uint64_t usage, uint64_t *addr) {
    if (fd < 0) {
        ALOGE("%s: invalid parameters", __func__);
        return -EINVAL;
    }

    auto heap = pick_dma_buf_heap(usage);
    if ((heap == dma_buf_heap::system) || (heap == dma_buf_heap::system_uncached)) {
        ALOGW("%s: no physical address for system heap", __func__);
        return -EINVAL;
    }

    uint64_t phy_addr = -1;
    struct dmabuf_imx_phys_data data;
    int fd_;
    fd_ = open("/dev/dmabuf_imx", O_RDONLY | O_CLOEXEC);
    if (fd_ < 0) {
        ALOGE("%s: open /dev/dmabuf_imx failed: %s", __func__, strerror(errno));
        return -EINVAL;
    }
    data.dmafd = fd;
    if (ioctl(fd_, DMABUF_GET_PHYS, &data) < 0) {
        ALOGE("%s ioctl DMABUF_GET_PHYS failed", __func__);
        close(fd_);
        return -EINVAL;
    } else {
        phy_addr = data.phys;
    }
    close(fd_);

    *addr = phy_addr;
    return 0;
}
