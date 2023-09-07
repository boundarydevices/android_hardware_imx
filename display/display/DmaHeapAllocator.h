/*
 * Copyright 2021 NXP.
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

#ifndef _DMA_HEAP_ALLOCATOR_H_
#define _DMA_HEAP_ALLOCATOR_H_

#include <BufferAllocator/BufferAllocatorWrapper.h>
#include <utils/Mutex.h>

#include "Allocator.h"
#include "Memory.h"

namespace fsl {

using android::Mutex;

class DmaHeapAllocator : public Allocator {
public:
    static DmaHeapAllocator* getInstance();
    ~DmaHeapAllocator();

    // alloc system memory and return fd which represents this memory.
    int allocSystemMemeory(uint64_t size);
    // alloc memory and return fd which represents this memory.
    int allocMemory(int size, int align, int flags);
    // flush cacheable memory cache.
    int flushCache(int fd);
    // get contiguous memory physical address.
    int getPhys(int fd, int size, uint64_t& addr);
    // get memory virtual address.
    int getVaddrs(int fd, int size, uint64_t& addr);
    int getHeapType(int fd);

private:
    DmaHeapAllocator();
    static DmaHeapAllocator* sInstance;
    BufferAllocator* mBufferAllocator;
    static Mutex sLock;
};

} // namespace fsl
#endif
