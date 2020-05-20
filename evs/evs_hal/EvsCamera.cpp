/*
 * Copyright (C) 2016 The Android Open Source Project
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
#include <log/log.h>
#include "EvsCamera.h"

namespace android {
namespace hardware {
namespace automotive {
namespace evs {
namespace V1_0 {
namespace implementation {

#define CAMERA_WIDTH 1280
#define CAMERA_HEIGHT 720

void EvsCamera::EvsAppRecipient::serviceDied(uint64_t /*cookie*/,
        const ::android::wp<::android::hidl::base::V1_0::IBase>& /*who*/)
{
    mCamera->releaseResource();
}

void EvsCamera::releaseResource(void)
{
    shutdown();
}

EvsCamera::EvsCamera(const char *deviceName)
{
    ALOGD("EvsCamera instantiated");

    // Initialize the stream params.
    mFormat = fsl::FORMAT_YUYV;
    mWidth = CAMERA_WIDTH;
    mHeight = CAMERA_HEIGHT;
    mDeqIdx = -1;
    mDescription.cameraId = deviceName;
}

EvsCamera::~EvsCamera()
{
    ALOGD("EvsCamera being destroyed");
}

void EvsCamera::openup(const char *deviceName)
{
    // Initialize the video device
    if (!onOpen(deviceName)) {
        ALOGE("Failed to open v4l device %s\n", deviceName);
        return;
    }

    // Initialize memory.
    onMemoryCreate();
}

//
// This gets called if another caller "steals" ownership of the camera
//
void EvsCamera::shutdown()
{
    ALOGD("EvsCamera shutdown");

    // Make sure our output stream is cleaned up
    // (It really should be already)
    stopVideoStream();

    // Close our video capture device
    onClose();

    // Destroy memory.
    onMemoryDestroy();
}


// Methods from ::android::hardware::automotive::evs::V1_0::IEvsCamera follow.
Return<void> EvsCamera::getCameraInfo(getCameraInfo_cb _hidl_cb) {
    ALOGD("getCameraInfo");

    // Send back our self description
    _hidl_cb(mDescription);
    return Void();
}


Return<EvsResult> EvsCamera::setMaxFramesInFlight(uint32_t bufferCount) {
    ALOGD("setMaxFramesInFlight");
    // If we've been displaced by another owner of the camera,
    // then we can't do anything else
    if (!isOpen()) {
        ALOGW("ignoring call when camera has been lost.");
        return EvsResult::OWNERSHIP_LOST;
    }

    // We cannot function without at least one video buffer to send data
    if (bufferCount < 1) {
        ALOGE("Ignoring with less than one buffer requested");
        return EvsResult::INVALID_ARG;
    }

    // Update our internal state
    return EvsResult::OK;
}

Return<int32_t> EvsCamera::getExtendedInfo(uint32_t /*opaqueIdentifier*/)
{
    ALOGD("getExtendedInfo");
    // Return zero by default as required by the spec
    return 0;
}

Return<EvsResult> EvsCamera::setExtendedInfo(uint32_t /*opaqueIdentifier*/,
                                                int32_t /*opaqueValue*/)
{
    ALOGD("setExtendedInfo");
    // If we've been displaced by another owner of the camera,
    // then we can't do anything else
    if (!isOpen()) {
        ALOGW("ignoring setExtendedInfo call when camera has been lost.");
        return EvsResult::OWNERSHIP_LOST;
    }

    // We don't store any device specific information in this implementation
    return EvsResult::INVALID_ARG;
}

Return<EvsResult> EvsCamera::startVideoStream(
        const ::android::sp<IEvsCameraStream>& stream)
{
    ALOGD("startVideoStream");
    // If we've been displaced by another owner of the camera,
    // then we can't do anything else
    if (!isOpen()) {
        ALOGW("ignoring startVideoStream call when camera has been lost.");
        return EvsResult::OWNERSHIP_LOST;
    }

    int prevRunMode;
    {
        std::unique_lock <std::mutex> lock(mLock);
        // Set the state of our background thread
        prevRunMode = mRunMode.fetch_or(RUN);
    }

    if (prevRunMode & RUN) {
        // The background thread is running, so we can't start a new stream
        ALOGE("Already in RUN state, so we can't start a new streaming thread");
        return EvsResult::UNDERLYING_SERVICE_ERROR;
    }

    sp<EvsAppRecipient> appRecipient = nullptr;
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (mStream.get() != nullptr) {
            ALOGE("ignoring startVideoStream call when a stream is running.");
            return EvsResult::STREAM_ALREADY_RUNNING;
        }

        // Record the user's callback for use when we have a frame ready
        mStream = stream;
        mEvsAppRecipient = new EvsAppRecipient(this);
        appRecipient = mEvsAppRecipient;
    }

    // Set up the video stream.
    if (!onStart()) {
        ALOGE("underlying camera start stream failed");
        {
            std::lock_guard<std::mutex> lock(mLock);
            // No need to hold onto this if we failed to start.
            mStream = nullptr;
            mEvsAppRecipient = nullptr;
        }
        shutdown();
        return EvsResult::UNDERLYING_SERVICE_ERROR;
    }
    stream->linkToDeath(appRecipient, 0);

    std::unique_lock <std::mutex> lock(mLock);
    // Fire up a thread to receive and dispatch the video frames
    mCaptureThread = std::thread([this](){collectFrames();});


    return EvsResult::OK;
}

Return<void> EvsCamera::stopVideoStream()
{
    ALOGD("stopVideoStream");

    int prevRunMode;
    std::thread thread;
    {
        std::unique_lock <std::mutex> lock(mLock);
        // Tell the background thread to stop
        prevRunMode = mRunMode.fetch_or(STOPPING);
        thread.swap(mCaptureThread);
    }

    if (prevRunMode == STOPPED) {
        std::unique_lock <std::mutex> lock(mLock);
        // The background thread wasn't running, so set the flag back to STOPPED
        mRunMode = STOPPED;
    }
    else if (prevRunMode & STOPPING) {
        ALOGE("stopStream called while stream is already stopping.");
        ALOGE("Reentrancy is not supported!");
    }
    else {
        {
            std::unique_lock <std::mutex> lock(mLock);
            mRunMode = STOPPED;
        }

        // Tell the capture device to stop (and block until it does)
        onStop();
        // Block until the background thread is stopped
        if (thread.joinable()) {
            thread.join();
        }

        ALOGD("Capture thread stopped.");
    }


    ::android::sp<IEvsCameraStream> stream = nullptr;
    sp<EvsAppRecipient> appRecipient = nullptr;
    {
        std::unique_lock <std::mutex> lock(mLock);
        stream = mStream;
        appRecipient = mEvsAppRecipient;

        // Drop our reference to the client's stream receiver
        mStream = nullptr;
        mEvsAppRecipient = nullptr;
    }

    if (stream != nullptr) {
        // Send one last NULL frame to signal the actual end of stream
        BufferDesc nullBuff = {};
        auto result = stream->deliverFrame(nullBuff);
        if (!result.isOk()) {
            ALOGE("Error delivering end of stream marker");
        }

        stream->unlinkToDeath(appRecipient);
    }

    return Void();
}

Return<void> EvsCamera::doneWithFrame(const BufferDesc& buffer)
{
    ALOGV("doneWithFrame index %d", buffer.bufferId);
    // If we've been displaced by another owner of the camera
    // then we can't do anything else
    if (!isOpen()) {
        ALOGW("ignoring doneWithFrame call when camera has been lost.");
    }

    if (buffer.memHandle == nullptr) {
        ALOGE("ignoring doneWithFrame called with null handle");
        return Void();
    }

    onFrameReturn(buffer.bufferId);

    return Void();
}

// This is the async callback from the thread that tells us a frame is ready
void EvsCamera::forwardFrame(fsl::Memory* handle, int index)
{
    // Assemble the buffer description we'll transmit below
    BufferDesc buff = {};
    buff.width      = handle->width;
    buff.height     = handle->height;
    buff.stride     = handle->stride;
    buff.format     = handle->fslFormat;
    buff.usage      = handle->usage;
    buff.bufferId   = index;
    buff.memHandle  = handle;

    ::android::sp<IEvsCameraStream> stream = nullptr;
    {
        std::unique_lock <std::mutex> lock(mLock);
        stream = mStream;
    }
    // Issue the (asynchronous) callback to the client
    if (stream != nullptr) {
        auto result = stream->deliverFrame(buff);
        if (result.isOk()) {
            ALOGV("Delivered %p as id %d",
                buff.memHandle.getNativeHandle(), buff.bufferId);
            return;
        }
    }

    // This can happen if the client dies and is likely unrecoverable.
    // To avoid consuming resources generating failing calls, we stop sending
    // frames.  Note, however, that the stream remains in the "STREAMING" state
    // until cleaned up on the main thread.
    ALOGE("Frame delivery call failed in the transport layer.");
    onFrameReturn(index);
}

// This runs on a background thread to receive and dispatch video frames
void EvsCamera::collectFrames()
{
    int runMode;
    {
        std::unique_lock <std::mutex> lock(mLock);
        runMode = mRunMode;
    }

    fsl::Memory *buffer = nullptr;
    int index = -1;
    // Run until our atomic signal is cleared
    while (runMode == RUN) {
        // Wait for a buffer to be ready
        buffer = onFrameCollect(index);
        if (buffer != nullptr) {
            forwardFrame(buffer, index);
        }

        std::unique_lock <std::mutex> lock(mLock);
        runMode = mRunMode;
    }

    // Mark ourselves stopped
    ALOGD("%s thread ending", __func__);
}

void EvsCamera::onMemoryCreate()
{
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

void EvsCamera::onMemoryDestroy()
{
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

} // namespace implementation
} // namespace V1_0
} // namespace evs
} // namespace automotive
} // namespace hardware
} // namespace android
