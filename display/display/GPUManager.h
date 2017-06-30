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
#ifndef _FSL_GPU_MANAGER_H_
#define _FSL_GPU_MANAGER_H_

#include "Memory.h"
#include "MemoryManager.h"
#include "IonManager.h"

#include <hardware/gralloc.h>

namespace fsl {

class GPUShadow : public MemoryShadow
{
public:
    GPUShadow(struct Memory* handle, bool own,
              alloc_device_t* alloc, gralloc_module_t* module);
    ~GPUShadow();

    struct Memory* handle();

private:
    struct Memory* mHandle;
    alloc_device_t *mAlloc;
    gralloc_module_t* mModule;
};

class GPUManager : public MemoryManager
{
public:
    GPUManager();
    ~GPUManager();

    bool isValid();
    virtual int allocMemory(MemoryDesc& desc, Memory** out);

    virtual int retainMemory(Memory* handle);
    virtual int lock(Memory* handle, int usage,
            int l, int t, int w, int h,
            void** vaddr);
    virtual int lockYCbCr(Memory* handle, int usage,
            int l, int t, int w, int h,
            android_ycbcr* ycbcr);
    virtual int unlock(Memory* handle);

private:
    alloc_device_t *mAlloc;
    gralloc_module_t* mModule;
    IonManager* mIonManager;
};

}
#endif
