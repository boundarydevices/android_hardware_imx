/*
 * Copyright 2024 NXP.
 */
#ifndef DMA_BUF_HEAPS_H
#define DMA_BUF_HEAPS_H

#include "gralloc_handle.h"

int allocator_allocate_system_memory(uint64_t size);
gralloc_handle *allocator_allocate(const gralloc_buffer_descriptor *descriptor);
int allocator_sync_start(gralloc_handle_t handle, bool read, bool write);
int allocator_sync_end(gralloc_handle_t handle, bool read, bool write);
int allocator_map(gralloc_handle_t handle);
void allocator_unmap(gralloc_handle_t handle);
bool allocator_supports_protected_memory(uint64_t usage);
void allocator_close();
int allocator_get_physical_address(int fd, uint64_t usage, uint64_t *addr);

#endif
