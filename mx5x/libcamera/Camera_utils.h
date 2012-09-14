
/*
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

/*
 * Copyright 2009-2012 Freescale Semiconductor, Inc. All Rights Reserved.
 */

#ifndef CAMERA_UTILS_H
#define CAMERA_UTILS_H

#undef LOG_TAG
#define LOG_TAG "FslCameraHAL"
#include <utils/Log.h>
#include <utils/threads.h>

//#define CAMERA_HAL_DEBUG_LOG

#ifdef CAMERA_HAL_DEBUG_LOG
#define CAMERA_HAL_LOG_RUNTIME(format, ...) ALOGI((format), ## __VA_ARGS__)
#define CAMERA_HAL_LOG_FUNC ALOGI("%s is excuting...",  __FUNCTION__)
#define CAMERA_HAL_LOG_TRACE   ALOGI("%s : %d", __FUNCTION__,__LINE__)
#define CAMERA_HAL_LOG_INFO(format, ...) ALOGI((format), ## __VA_ARGS__)
#else
#define CAMERA_HAL_LOG_RUNTIME(format, ...) 
#define CAMERA_HAL_LOG_FUNC
#define CAMERA_HAL_LOG_TRACE
#define CAMERA_HAL_LOG_INFO(format, ...)
#endif

#define CAMERA_HAL_ERR(format, ...) ALOGE((format), ##__VA_ARGS__)

namespace android {

    typedef enum{
        DMA_ALLOCATE_ERR_NONE = 0,
        DMA_ALLOCATE_ERR_BAD_PARAM = -1,

    }DMA_ALLOCATE_ERR_RET;

    typedef enum{
        WINDOW_BUFS_INVALID = 0,
        WINDOW_BUFS_DEQUEUED = 1,
        WINDOW_BUFS_QUEUED = 2,
    }WINDOW_BUFS_STATE;

    typedef struct {
        unsigned char *virt_start;
        size_t phy_offset;
        unsigned int length;
        void *native_buf;
        Mutex mBufferLock;
        unsigned int refCount;
        WINDOW_BUFS_STATE buf_state;
    }DMA_BUFFER;

	// If struct change. Need info Camera Source.
    typedef struct {
        size_t phy_offset;
        unsigned int length;
    }VIDEOFRAME_BUFFER_PHY;

}; //name space android

#endif

