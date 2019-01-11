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

#include "EvsDisplay.h"

#include <sync/sync.h>

namespace android {
namespace hardware {
namespace automotive {
namespace evs {
namespace V1_0 {
namespace implementation {

using namespace android;
using ::nxp::hardware::display::V1_0::Error;

#define DISPLAY_WIDTH 1280
#define DISPLAY_HEIGHT 720

EvsDisplay::EvsDisplay()
{
    ALOGD("EvsDisplay instantiated");

    // Set up our self description
    // NOTE:  These are arbitrary values chosen for testing
    mInfo.displayId   = "evs hal Display";
    mInfo.vendorFlags = 3870;

    mWidth = DISPLAY_WIDTH;
    mHeight = DISPLAY_HEIGHT;
    mFormat = HAL_PIXEL_FORMAT_RGBA_8888;

    initialize();
}

EvsDisplay::~EvsDisplay()
{
    ALOGD("EvsDisplay being destroyed");
    forceShutdown();
}

void EvsDisplay::showWindow()
{
    ALOGI("%s window is showing", __func__);
}


void EvsDisplay::hideWindow()
{
}

// Main entry point
bool EvsDisplay::initialize()
{
    //
    //  Create the native full screen window and get a suitable configuration to match it
    //
    uint32_t layer = -1;
    sp<IDisplay> display = nullptr;
    while (display.get() == nullptr) {
        display = IDisplay::getService();
        if (display.get() == nullptr) {
            ALOGE("%s get display service failed", __func__);
            usleep(200000);
        }
    }

    display->getLayer(DISPLAY_BUFFER_NUM,
        [&](const auto& tmpError, const auto& tmpLayer) {
            if (tmpError == Error::NONE) {
                layer = tmpLayer;
            }
    });

    if (layer == (uint32_t)-1) {
        ALOGE("%s get layer failed", __func__);
        return false;
    }

    {
        std::unique_lock<std::mutex> lock(mLock);
        mIndex = 0;
        mDisplay = display;
        mLayer = layer;
    }

    // allocate memory.
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
        return false;
    }

    for (int i = 0; i < DISPLAY_BUFFER_NUM; i++) {
        buffer = nullptr;
        allocator->allocMemory(desc, &buffer);

        std::unique_lock<std::mutex> lock(mLock);
        mBuffers[i] = buffer;
    }

    return true;
}

/**
 * This gets called if another caller "steals" ownership of the display
 */
void EvsDisplay::forceShutdown()
{
    ALOGD("EvsDisplay forceShutdown");
    int layer;
    sp<IDisplay> display;
    {
        std::unique_lock<std::mutex> lock(mLock);
        layer = mLayer;
        mLayer = -1;
        display = mDisplay;
    }

    if (display != nullptr) {
        display->putLayer(layer);
    }

    fsl::Memory *buffer = nullptr;
    fsl::MemoryManager* allocator = fsl::MemoryManager::getInstance();
    for (int i = 0; i < DISPLAY_BUFFER_NUM; i++) {
        {
            std::unique_lock<std::mutex> lock(mLock);
            if (mBuffers[i] == nullptr) {
                continue;
            }

            buffer = mBuffers[i];
            mBuffers[i] = nullptr;
        }
        allocator->releaseMemory(buffer);
    }

    std::lock_guard<std::mutex> lock(mLock);
    // Put this object into an unrecoverable error state since somebody else
    // is going to own the display now.
    mRequestedState = DisplayState::DEAD;
}

/**
 * Returns basic information about the EVS display provided by the system.
 * See the description of the DisplayDesc structure for details.
 */
Return<void> EvsDisplay::getDisplayInfo(getDisplayInfo_cb _hidl_cb)
{
    ALOGD("getDisplayInfo");

    // Send back our self description
    _hidl_cb(mInfo);
    return Void();
}

/**
 * Clients may set the display state to express their desired state.
 * The HAL implementation must gracefully accept a request for any state
 * while in any other state, although the response may be to ignore the request.
 * The display is defined to start in the NOT_VISIBLE state upon initialization.
 * The client is then expected to request the VISIBLE_ON_NEXT_FRAME state, and
 * then begin providing video.  When the display is no longer required, the client
 * is expected to request the NOT_VISIBLE state after passing the last video frame.
 */
Return<EvsResult> EvsDisplay::setDisplayState(DisplayState state)
{
    ALOGD("setDisplayState");

    {
        std::lock_guard<std::mutex> lock(mLock);

        if (mRequestedState == DisplayState::DEAD) {
            // This object no longer owns the display -- it's been superceeded!
            return EvsResult::OWNERSHIP_LOST;
        }
    }

    // Ensure we recognize the requested state so we don't go off the rails
    if (state >= DisplayState::NUM_STATES) {
        return EvsResult::INVALID_ARG;
    }

    switch (state) {
    case DisplayState::NOT_VISIBLE:
        hideWindow();
        break;
    case DisplayState::VISIBLE:
        showWindow();
        break;
    default:
        break;
    }

    std::lock_guard<std::mutex> lock(mLock);
    // Record the requested state
    mRequestedState = state;

    return EvsResult::OK;
}

/**
 * The HAL implementation should report the actual current state, which might
 * transiently differ from the most recently requested state.  Note, however, that
 * the logic responsible for changing display states should generally live above
 * the device layer, making it undesirable for the HAL implementation to
 * spontaneously change display states.
 */
Return<DisplayState> EvsDisplay::getDisplayState()
{
    ALOGD("getDisplayState");

    std::lock_guard<std::mutex> lock(mLock);
    return mRequestedState;
}

/**
 * This call returns a handle to a frame buffer associated with the display.
 * This buffer may be locked and written to by software and/or GL.  This buffer
 * must be returned via a call to returnTargetBufferForDisplay() even if the
 * display is no longer visible.
 */
Return<void> EvsDisplay::getTargetBuffer(getTargetBuffer_cb _hidl_cb)
{
    ALOGV("getTargetBuffer");

    BufferDesc hbuf = {};
    {
        std::lock_guard<std::mutex> lock(mLock);

        if (mRequestedState == DisplayState::DEAD) {
            ALOGE("Rejecting buffer request from object that lost ownership of the display.");
            _hidl_cb(hbuf);
            return Void();
        }
    }

    int layer;
    uint32_t slot = -1;
    sp<IDisplay> display = nullptr;
    {
        std::lock_guard<std::mutex> lock(mLock);
        display = mDisplay;
        layer = mLayer;
    }
    if (display == nullptr)  {
        ALOGE("%s invalid display", __func__);
        _hidl_cb(hbuf);
        return Void();
    }

    display->getSlot(layer, [&](const auto& tmpError, const auto& tmpSlot) {
        if (tmpError == Error::NONE) {
            slot = tmpSlot;
        }
    });

    if (slot == (uint32_t)-1) {
        ALOGE("%s get slot failed", __func__);
        _hidl_cb(hbuf);
        return Void();
    }

    fsl::Memory *buffer = nullptr;
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (mBuffers[slot] == nullptr) {
            ALOGE("%s can't find valid buffer", __func__);
            _hidl_cb(hbuf);
            return Void();
        }
        buffer = mBuffers[slot];
    }

    // Assemble the buffer description we'll use for our render target
    // hard code the resolution 640*480
    hbuf.width     = buffer->width;
    hbuf.height    = buffer->height;
    hbuf.stride    = buffer->stride;
    hbuf.format    = buffer->format;
    hbuf.usage     = buffer->usage;
    hbuf.bufferId  = slot;
    hbuf.pixelSize = 4;
    hbuf.memHandle = buffer;

    // Send the buffer to the client
    ALOGV("Providing display buffer handle %p as id %d",
          hbuf.memHandle.getNativeHandle(), hbuf.bufferId);
    _hidl_cb(hbuf);
    return Void();
}


/**
 * This call tells the display that the buffer is ready for display.
 * The buffer is no longer valid for use by the client after this call.
 */
Return<EvsResult> EvsDisplay::returnTargetBufferForDisplay(const BufferDesc& buffer)
{
    ALOGV("returnTargetBufferForDisplay %p", buffer.memHandle.getNativeHandle());
    // Nobody should call us with a null handle
    if (!buffer.memHandle.getNativeHandle()) {
        ALOGE ("%s invalid buffer handle.\n", __func__);
        return EvsResult::INVALID_ARG;
    }

    if (buffer.bufferId >= DISPLAY_BUFFER_NUM) {
        ALOGE ("%s invalid buffer id.\n", __func__);
        return EvsResult::INVALID_ARG;
    }

    DisplayState state;
    sp<IDisplay> display;
    fsl::Memory *abuffer = nullptr;
    int layer;
    {
        std::lock_guard<std::mutex> lock(mLock);
        state = mRequestedState;
        abuffer = mBuffers[buffer.bufferId];
        display = mDisplay;
        layer = mLayer;
    }

    if (abuffer == nullptr) {
        ALOGE ("%s abuffer invalid.\n", __func__);
        return EvsResult::INVALID_ARG;
    }

    if (display != nullptr) {
        display->presentLayer(layer, buffer.bufferId, abuffer);
    }

    // If we've been displaced by another owner of the display, then we can't do anything else
    if (state == DisplayState::DEAD) {
        return EvsResult::OWNERSHIP_LOST;
    }

    // If we were waiting for a new frame, this is it!
    if (state == DisplayState::VISIBLE_ON_NEXT_FRAME) {
        showWindow();
        std::lock_guard<std::mutex> lock(mLock);
        mRequestedState = DisplayState::VISIBLE;
    }

    // Validate we're in an expected state
    if (state != DisplayState::VISIBLE) {
        // Not sure why a client would send frames back when we're not visible.
        ALOGW("Got a frame returned while not visible - ignoring.\n");
    }
    else {
        ALOGV("Got a visible frame %d returned.\n", buffer.bufferId);
    }

    return EvsResult::OK;
}

} // namespace implementation
} // namespace V1_0
} // namespace evs
} // namespace automotive
} // namespace hardware
} // namespace android
