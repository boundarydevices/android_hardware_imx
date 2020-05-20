/*
 * Copyright 2019 NXP.
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
#include <stdio.h>
#include <stdlib.h>
#include <error.h>
#include <errno.h>
#include <memory.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <log/log.h>
#include <inttypes.h>

#include "assert.h"

#include "FakeCapture.h"

#define COLOR_HEIGHT 64
#define COLOR_VALUE 0x86

FakeCapture::FakeCapture(const char *deviceName)
    : EvsCamera(deviceName)
{
}

FakeCapture::~FakeCapture()
{
}

// open v4l2 device.
bool FakeCapture::onOpen(const char* /*deviceName*/)
{
    ALOGI("Current output format: fmt=0x%X, %dx%d", mFormat, mWidth, mHeight);

    std::unique_lock <std::mutex> lock(mLock);
    // Make sure we're initialized to the STOPPED state
    mRunMode = STOPPED;

    // Ready to go!
    return true;
}

bool FakeCapture::isOpen()
{
    return true;
}

void FakeCapture::onClose()
{
    ALOGD("FakeCapture::close");

    {
        std::unique_lock <std::mutex> lock(mLock);
        // Stream should be stopped first!
        assert(mRunMode == STOPPED);
    }
}

bool FakeCapture::onStart()
{
    fsl::Memory *buffer = nullptr;
    for (int i = 0; i < CAMERA_BUFFER_NUM; i++) {
        // Get the information on the buffer that was created for us
        {
            std::unique_lock <std::mutex> lock(mLock);
            buffer = mBuffers[i];
        }
        if (buffer == nullptr) {
            ALOGE("%s buffer not ready!", __func__);
            return false;
        }

        ALOGI("Buffer description:");
        ALOGI("phys: 0x%" PRIx64, buffer->phys);
        ALOGI("length: %d", buffer->size);
    }

    std::unique_lock <std::mutex> lock(mLock);
    mDeqIdx = 0;

    ALOGD("Stream started.");
    return true;
}


void FakeCapture::onStop()
{
    ALOGD("Stream stopped.");
}

bool FakeCapture::onFrameReturn(int index)
{
    // We're giving the frame back to the system, so clear the "ready" flag
    ALOGV("returnFrame  index %d", index);
    if (index < 0 || index >= CAMERA_BUFFER_NUM) {
        ALOGE("%s invalid index:%d", __func__, index);
        return false;
    }

    fsl::Memory *buffer = nullptr;
    {
        std::unique_lock <std::mutex> lock(mLock);
        buffer = mBuffers[index];
    }

    if (buffer == nullptr) {
        ALOGE("%s invalid buffer", __func__);
        return false;
    }

    return true;
}

// This runs on a background thread to receive and dispatch video frames
fsl::Memory* FakeCapture::onFrameCollect(int &index)
{
    fsl::Memory *buffer = nullptr;
    usleep(33333);//30fps

    {
        std::unique_lock <std::mutex> lock(mLock);
        buffer = mBuffers[mDeqIdx];
        index = mDeqIdx++;
        mDeqIdx = mDeqIdx % CAMERA_BUFFER_NUM;
        mFrameIndex++;
    }

    if (buffer == nullptr || buffer->base == 0) {
        ALOGE("%s invalid buffer", __func__);
        return nullptr;
    }

    void *vaddr = nullptr;
    int32_t startLine = 0;
    vaddr = (void *)buffer->base;
    memset(vaddr, 0x0, buffer->stride * buffer->height);

    startLine = mFrameIndex % (buffer->height / COLOR_HEIGHT) * COLOR_HEIGHT;
    if(startLine + COLOR_HEIGHT <= buffer->height) {
        memset((char *)vaddr + startLine * buffer->stride,
                COLOR_VALUE, buffer->stride*COLOR_HEIGHT);
    }

    return buffer;
}

void FakeCapture::onMemoryCreate()
{
    EvsCamera::onMemoryCreate();

    void *vaddr;
    fsl::Memory *buffer = nullptr;
    fsl::MemoryManager* allocator = fsl::MemoryManager::getInstance();
    for (int i = 0; i < CAMERA_BUFFER_NUM; i++) {
        {
            std::unique_lock <std::mutex> lock(mLock);
            buffer = mBuffers[i];
        }
        allocator->lock(buffer, buffer->usage, 0, 0,
                buffer->width, buffer->height, &vaddr);
    }
}
