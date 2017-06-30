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

#include "Memory.h"
#include "MemoryManager.h"

namespace fsl {

typedef int (*gpu_wrapfunc)(void* handle, int w, int h, int f,
                            int s, int phys, void* addr);
typedef int (*gpu_unwrapfunc)(void* handle);

class IonShadow : public MemoryShadow
{
public:
    IonShadow(int fd, struct Memory* handle, bool own, gpu_unwrapfunc pointer);
    ~IonShadow();

    int fd();
    struct Memory* handle();

private:
    int mFd;
    struct Memory* mHandle;
    gpu_unwrapfunc mUnwrap;
};

class IonManager : public MemoryManager
{
public:
    IonManager();
    ~IonManager();

    virtual int allocMemory(MemoryDesc& desc, Memory** out);

    int flushCache(Memory* memory);
    int getPhys(Memory* memory);
    int getVaddrs(Memory* memory);

    virtual int retainMemory(Memory* handle);
    virtual int lock(Memory* handle, int usage,
            int l, int t, int w, int h,
            void** vaddr);
    virtual int lockYCbCr(Memory* handle, int usage,
            int l, int t, int w, int h,
            android_ycbcr* ycbcr);
    virtual int unlock(Memory* handle);

private:
    int mIonFd;
    gpu_wrapfunc mWrap;
    gpu_unwrapfunc mUnwrap;
};

}
#endif
