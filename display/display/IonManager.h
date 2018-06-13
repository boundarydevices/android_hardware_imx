/*
 * Copyright (C) 2013-2016 Freescale Semiconductor, Inc.
 * Copyright 2017 NXP.
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

#ifndef _FSL_ION_MANAGER_H_
#define _FSL_ION_MANAGER_H_

#include <hardware/gralloc.h>
#include "Memory.h"
#include "MemoryDesc.h"
#include "IonAllocator.h"

namespace fsl {

class IonManager
{
public:
    IonManager();
    ~IonManager();

    int allocMemory(MemoryDesc& desc, Memory** out);

    int flushCache(Memory* memory);
    int getPhys(Memory* memory);
    int getVaddrs(Memory* memory);

    int lock(Memory* handle, int usage,
            int l, int t, int w, int h,
            void** vaddr);
    int lockYCbCr(Memory* handle, int usage,
            int l, int t, int w, int h,
            android_ycbcr* ycbcr);
    int unlock(Memory* handle);

private:
    IonAllocator* mAllocator;
};

}
#endif
