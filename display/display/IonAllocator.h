/*
 * Copyright 2018 NXP.
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

#ifndef _FSL_ION_ALLOCATOR_H_
#define _FSL_ION_ALLOCATOR_H_

#include <utils/Mutex.h>

namespace fsl {

#define ION_MEM_ALIGN 8

enum MEM_FLAGS {
    MFLAGS_CONTIGUOUS = 1,
    MFLAGS_CACHEABLE  = 2,
    MFLAGS_SECURE     = 4
};

using android::Mutex;

class IonAllocator
{
public:
    static IonAllocator* getInstance();
    ~IonAllocator();

    // alloc memory and return fd which represents this memory.
    int allocMemory(int size, int align, int flags);
    // flush cacheable memory cache.
    int flushCache(int fd);
    // get contiguous memory physical address.
    int getPhys(int fd, int size, uint64_t& addr);
    // get memory virtual address.
    int getVaddrs(int fd, int size, uint64_t& addr);

private:
    IonAllocator();
    static Mutex sLock;
    static IonAllocator *sInstance;

    int mIonFd;
    // contiguous cacheable memory ion heap ids.
    int mCCHeapIds;
    // contiguous non-cacheable memory ion heap ids.
    int mCNHeapIds;
    // non-contiguous cacheable memory ion heap ids.
    int mNCHeapIds;
    // secure ion heap ids.
    int mSeHeapIds;
};

}
#endif
