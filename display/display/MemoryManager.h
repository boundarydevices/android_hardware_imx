/*
 * Copyright (C) 2013-2015 Freescale Semiconductor, Inc.
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

#ifndef _FSL_MEMORY_MANAGER_H
#define _FSL_MEMORY_MANAGER_H

#include <hardware/gralloc.h>
#include <hardware/gralloc1.h>
#include <media/hardware/VideoAPI.h>
#include <utils/KeyedVector.h>

#include "IonManager.h"
#include "Memory.h"
#include "MemoryDesc.h"

namespace fsl {

using android::ColorAspects;
using android::HDRStaticInfo;
using android::KeyedVector;

struct MetaData {
    int32_t mFlags;
    uint32_t mYOffset;
    uint32_t mUVOffset;
};

class MemoryManager {
public:
    virtual ~MemoryManager();

    static MemoryManager* getInstance();

    // allocate system memory
    int allocSystemMemeory(uint64_t size);
    // allocate memory interface.
    int allocMemory(MemoryDesc& desc, Memory** out);
    // release memory interface.
    int releaseMemory(Memory* handle);

    // keep memory reference and import it.
    int retainMemory(Memory* handle);
    // lock memory to get virtual address for CPU access.
    int lock(Memory* handle, int usage, int l, int t, int w, int h, void** vaddr);
    // lock YUV memory to get virtual address for CPU access.
    int lockYCbCr(Memory* handle, int usage, int l, int t, int w, int h, android_ycbcr* ycbcr);
    // unlock memory after CPU access.
    int unlock(Memory* handle);
    // validate memory size
    int validateMemory(MemoryDesc& desc, Memory* handle);
    // flush memory
    int flush(Memory* handle);

    MetaData* getMetaData(Memory* handle);
    uint64_t getDrmModifier(Memory* buffer);

protected:
    MemoryManager();
    bool isDrmAlloc(int flags, int format, int usage);
    int allocMetaData(Memory* handle);

private:
    IonManager* mIonManager;
    alloc_device_t* mGPUAlloc;
    gralloc_module_t* mGPUModule;
    KeyedVector<Memory*, uint64_t> mMetaMap;

private:
    static Mutex sLock;
    static MemoryManager* sInstance;
};

} // namespace fsl
#endif
