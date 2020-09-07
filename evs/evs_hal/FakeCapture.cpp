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
#define CAMERA_WIDTH 1280
#define CAMERA_HEIGHT 720

FakeCapture::FakeCapture(const char *deviceName)
    : EvsCamera(deviceName)
{
    mWidth = CAMERA_WIDTH;
    mHeight = CAMERA_HEIGHT;
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

bool FakeCapture::onFrameReturn(int index, __attribute__ ((unused))std::string deviceid)
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
void FakeCapture::onFrameCollect(std::vector<struct forwardframe> &frames)
{
    struct forwardframe frame;
    fsl::Memory *buffer = nullptr;
    usleep(33333);//30fps

    {
        std::unique_lock <std::mutex> lock(mLock);
        buffer = mBuffers[mDeqIdx];
        mDeqIdx = mDeqIdx % CAMERA_BUFFER_NUM;
        mFrameIndex++;
    }

    if (buffer == nullptr || buffer->base == 0) {
        ALOGE("%s invalid buffer", __func__);
        return;
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

    frame.buf = buffer;
    frame.index = mDeqIdx;
    frames.push_back(frame);

    return;
}

int FakeCapture::getParameter(__attribute__((unused))v4l2_control& control) {
    return 0;
}

int FakeCapture::setParameter(__attribute__((unused))v4l2_control& control) {
    return 0;
}

std::set<uint32_t> FakeCapture::enumerateCameraControls() {
    std::set<uint32_t> ctrlIDs;
    return std::move(ctrlIDs);
}

void FakeCapture::onMemoryCreate() {
    fsl::Memory *buffer = nullptr;
    fsl::MemoryManager* allocator = fsl::MemoryManager::getInstance();
    fsl::MemoryDesc desc;
    desc.mWidth = mWidth;
    desc.mHeight = mHeight;
    desc.mFormat = mFormat;
    desc.mFslFormat = mFormat;
    desc.mProduceUsage |= fsl::USAGE_HW_TEXTURE
        | fsl::USAGE_HW_RENDER | fsl::USAGE_HW_VIDEO_ENCODER;
    desc.mFlag = 0;
    int ret = desc.checkFormat();
    if (ret != 0) {
        ALOGE("%s checkFormat failed", __func__);
        return;
    }
    for (int i = 0; i < CAMERA_BUFFER_NUM; i++) {
        buffer = nullptr;
        allocator->allocMemory(desc, &buffer);
        std::unique_lock <std::mutex> lock(mLock);
        mBuffers[i] = buffer;
    }
}

void FakeCapture::onMemoryDestroy() {
    fsl::Memory *buffer = nullptr;
    fsl::MemoryManager* allocator = fsl::MemoryManager::getInstance();
    for (int i = 0; i < CAMERA_BUFFER_NUM; i++) {
        {
            std::unique_lock <std::mutex> lock(mLock);
            if (mBuffers[i] == nullptr) {
                continue;
            }

            buffer = mBuffers[i];
            mBuffers[i] = nullptr;
        }
        allocator->releaseMemory(buffer);
    }
}
