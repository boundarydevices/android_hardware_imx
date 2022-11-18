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

#ifndef _FSL_ALLOCATOR_H_
#define _FSL_ALLOCATOR_H_

#include <utils/Mutex.h>
#include "Memory.h"

#define MEM_ALIGN 8
namespace fsl {

using android::Mutex;
class Allocator
{
public:
    static Allocator* getInstance();

    virtual ~Allocator() {};

    // alloc system memory and return fd which represents this memory.
    virtual int allocSystemMemeory(uint64_t size)=0;
    // alloc memory and return fd which represents this memory.
    virtual int allocMemory(int size, int align, int flags)=0;
    // flush cacheable memory cache.
    virtual int flushCache(int fd)=0;
    // get contiguous memory physical address.
    virtual int getPhys(int fd, int size, uint64_t& addr)=0;
    // get memory virtual address.
    virtual int getVaddrs(int fd, int size, uint64_t& addr)=0;

private:
    static Mutex sLock;
    static Allocator *mInstance;
};

}
#endif
