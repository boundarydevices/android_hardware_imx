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

#include <cutils/log.h>
#include "IonManager.h"
#include "GPUManager.h"

namespace fsl {

MemoryManager* MemoryManager::sInstance(0);
Mutex MemoryManager::sLock(Mutex::PRIVATE);

MemoryManager* MemoryManager::getInstance()
{
    Mutex::Autolock _l(sLock);
    if (sInstance != NULL) {
        return sInstance;
    }

    GPUManager* pManager = new GPUManager();
    if (pManager != NULL && pManager->isValid()) {
        ALOGI("GPUManager used");
        sInstance = pManager;
    }
    else {
        if (pManager != NULL) {
            delete pManager;
        }
        sInstance = new IonManager();
        ALOGI("IonManager used");
    }

    return sInstance;
}

MemoryManager::MemoryManager()
{
}

MemoryManager::~MemoryManager()
{
}

int MemoryManager::verifyMemory(Memory* handle)
{
    if (handle == NULL || !handle->isValid()) {
        ALOGE("%s invalid handle", __func__);
        return -EINVAL;
    }

    if (handle->pid != getpid()) {
        ALOGE("%s handle not allocated in this process", __func__);
        return -EINVAL;
    }

    MemoryShadow* shadow = (MemoryShadow*)(uintptr_t)handle->shadow;
    if (shadow == NULL) {
        ALOGE("%s buffer shadow invalid", __func__);
        return -EINVAL;
    }

    return 0;
}

int MemoryManager::releaseMemory(Memory* handle)
{
    if (handle == NULL || !handle->isValid()) {
        ALOGE("%s invalid handle", __func__);
        return -EINVAL;
    }

    if (handle->pid != getpid()) {
        ALOGE("%s handle not allocated in this process", __func__);
        return -EINVAL;
    }

    MemoryShadow* shadow = (MemoryShadow*)(uintptr_t)handle->shadow;
    if (shadow == NULL) {
        ALOGE("%s buffer handle invalid", __func__);
        return -EINVAL;
    }

    shadow->decRef();

    return 0;
}

int MemoryManager::lockYCbCr(Memory* handle, int /*usage*/,
            int /*l*/, int /*t*/, int /*w*/, int /*h*/,
            android_ycbcr* ycbcr)
{
    if (handle == NULL || !handle->isValid() || ycbcr == NULL) {
        ALOGE("%s invalid handle", __func__);
        return -EINVAL;
    }

    switch (handle->fslFormat) {
        case FORMAT_NV12:
            ycbcr->ystride = handle->stride;
            ycbcr->cstride = ycbcr->ystride;
            ycbcr->y = (void*)handle->base;
            ycbcr->cb = (void*)(handle->base + handle->stride*handle->height);
            ycbcr->cr = (void*)((uintptr_t)ycbcr->cb + 1);
            ycbcr->chroma_step = 2;
            break;

        case FORMAT_NV21:
            ycbcr->ystride = handle->stride;
            ycbcr->cstride = ycbcr->ystride;
            ycbcr->y = (void*)handle->base;
            ycbcr->cr = (void*)(handle->base + handle->stride*handle->height);
            ycbcr->cb = (void*)((uintptr_t)ycbcr->cr + 1);
            ycbcr->chroma_step = 2;
            break;

        case FORMAT_I420:
            ycbcr->ystride = handle->stride;
            ycbcr->cstride = ycbcr->ystride / 2;
            ycbcr->y = (void*)handle->base;
            ycbcr->cb = (void*)(handle->base + handle->stride*handle->height);
            ycbcr->cr = (void*)((uintptr_t)ycbcr->cb + ycbcr->cstride*handle->height/2);
            ycbcr->chroma_step = 1;
            break;

        case FORMAT_YV12:
            ycbcr->ystride = handle->stride;
            ycbcr->cstride = ycbcr->ystride / 2;
            ycbcr->y = (void*)handle->base;
            ycbcr->cr = (void*)(handle->base + handle->stride*handle->height);
            ycbcr->cb = (void*)((uintptr_t)ycbcr->cr + ycbcr->cstride*handle->height/2);
            ycbcr->chroma_step = 1;
            break;

        default:
            ALOGE("%s not support format:0x%x", __func__, handle->format);
            return -EINVAL;
    }

    return 0;
}

}
