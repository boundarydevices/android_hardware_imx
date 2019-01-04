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
#include <Memory.h>
#include <ui/DisplayInfo.h>

namespace android {
namespace hardware {
namespace automotive {
namespace evs {
namespace V1_0 {
namespace implementation {

using namespace android;

#define DISPLAY_WIDTH 1280
#define DISPLAY_HEIGHT 720

EvsDisplay::EvsDisplay()
{
    ALOGD("EvsDisplay instantiated");

    // Set up our self description
    // NOTE:  These are arbitrary values chosen for testing
    mInfo.displayId   = "Mock Display";
    mInfo.vendorFlags = 3870;
    mFormat = PIXEL_FORMAT_RGBA_8888;

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
    sp<SurfaceControl> surfaceControl;
    {
        std::lock_guard<std::mutex> lock(mLock);
        surfaceControl = mSurfaceControl;
    }

    if (surfaceControl != nullptr) {
#if ANDROID_SDK_VERSION >= 28
        SurfaceComposerClient::Transaction{}
                .setLayer(surfaceControl, 0x7FFFFFFF)     // always on top
                .show(surfaceControl)
                .apply();
#else
        SurfaceComposerClient::openGlobalTransaction();
                surfaceControl->setLayer(0x7FFFFFFF);     // always on top
                surfaceControl->show();
                SurfaceComposerClient::closeGlobalTransaction();
#endif
    }
}


void EvsDisplay::hideWindow()
{
    ALOGI("%s widow is hiding", __func__);
    sp<SurfaceControl> surfaceControl;
    {
        std::lock_guard<std::mutex> lock(mLock);
        surfaceControl = mSurfaceControl;
    }

    if (surfaceControl != nullptr) {
#if ANDROID_SDK_VERSION >= 28
        SurfaceComposerClient::Transaction{}
                .hide(surfaceControl)
                .apply();
#else
        SurfaceComposerClient::openGlobalTransaction();
                surfaceControl->hide();
                SurfaceComposerClient::closeGlobalTransaction();
#endif
    }
}

// Main entry point
bool EvsDisplay::initialize()
{
    //
    //  Create the native full screen window and get a suitable configuration to match it
    //
    status_t err;

    mComposerClient = new SurfaceComposerClient();
    if (mComposerClient == nullptr) {
        ALOGE("SurfaceComposerClient couldn't be allocated");
        return false;
    }
    err = mComposerClient->initCheck();
    if (err != NO_ERROR) {
        ALOGE("SurfaceComposerClient::initCheck error: %#x", err);
        return false;
    }

    // Get main display parameters.
    sp <IBinder> mainDpy = SurfaceComposerClient::getBuiltInDisplay(
            ISurfaceComposer::eDisplayIdMain);
    DisplayInfo mainDpyInfo;
    err = SurfaceComposerClient::getDisplayInfo(mainDpy, &mainDpyInfo);
    if (err != NO_ERROR) {
        ALOGE("ERROR: unable to get display characteristics");
        return false;
    }

    if (mainDpyInfo.orientation != DISPLAY_ORIENTATION_0 &&
        mainDpyInfo.orientation != DISPLAY_ORIENTATION_180) {
        // rotated
        mWidth = mainDpyInfo.h;
        mHeight = mainDpyInfo.w;
    } else {
        mWidth = mainDpyInfo.w;
        mHeight = mainDpyInfo.h;
    }

    mSurfaceControl = mComposerClient->createSurface(
            String8("Evs Display"), mWidth, mHeight,
            mFormat, ISurfaceComposerClient::eOpaque);
    if (mSurfaceControl == nullptr || !mSurfaceControl->isValid()) {
        ALOGE("Failed to create SurfaceControl");
        return false;
    }

    mNativeWindow = mSurfaceControl->getSurface();

    // configure native window.
    native_window_api_connect(mNativeWindow.get(), NATIVE_WINDOW_API_EGL);
    native_window_set_usage(mNativeWindow.get(), fsl::USAGE_HW_TEXTURE
                  | fsl::USAGE_HW_RENDER); //fsl::USAGE_GPU_TILED_VIV
    native_window_set_buffer_count(mNativeWindow.get(), DISPLAY_BUFFER_NUM);
    memset(mBuffers, 0, sizeof(mBuffers));

    return true;
}

/**
 * This gets called if another caller "steals" ownership of the display
 */
void EvsDisplay::forceShutdown()
{
    ALOGD("EvsDisplay forceShutdown");
    sp<ANativeWindow> window;
    {
        std::lock_guard<std::mutex> lock(mLock);
        window = mNativeWindow;
    }

    if (window != nullptr) {
        native_window_api_disconnect(window.get(), NATIVE_WINDOW_API_EGL);
    }

    for (int i=0; i<DISPLAY_BUFFER_NUM; i++) {
        std::lock_guard<std::mutex> lock(mLock);
        if (mBuffers[i] == nullptr) {
            continue;
        }

        mBuffers[i]->decStrong(mBuffers[i]);
        mBuffers[i] = nullptr;
    }

    std::lock_guard<std::mutex> lock(mLock);
    // Let go of our SurfaceComposer resources
    mNativeWindow.clear();
    mSurfaceControl.clear();
    mComposerClient.clear();
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

    sp<ANativeWindow> window;
    BufferDesc hbuf = {};
    {
        std::lock_guard<std::mutex> lock(mLock);

        if (mRequestedState == DisplayState::DEAD) {
            ALOGE("Rejecting buffer request from object that lost ownership of the display.");
            _hidl_cb(hbuf);
            return Void();
        }
        window = mNativeWindow;
    }

    if (window == nullptr) {
        ALOGE("%s invalid window", __func__);
        _hidl_cb(hbuf);
        return Void();
    }

    ANativeWindowBuffer *buffer = nullptr;
    int fenceFd = -1;
    int ret = window->dequeueBuffer(window.get(), &buffer, &fenceFd);
    if (ret != 0) {
        ALOGE("getTargetBuffer called while no buffers available.");
        _hidl_cb(hbuf);
        return Void();
    }
    if (fenceFd != -1) {
        sync_wait(fenceFd, -1);
        close(fenceFd);
    }

    int index = -1;
    {
        std::lock_guard<std::mutex> lock(mLock);
        for (int i=0; i<DISPLAY_BUFFER_NUM; i++) {
            if (mBuffers[i] != nullptr) {
                continue;
            }

            mBuffers[i] = buffer;
            index = i;
            break;
        }
    }

    if (index == -1) {
        ALOGE("%s can't fine free slot", __func__);
        _hidl_cb(hbuf);
        return Void();
    }

    // reference to the buffer.
    buffer->incStrong(buffer);
    // Assemble the buffer description we'll use for our render target
    // hard code the resolution 640*480
    hbuf.width     = buffer->width;
    hbuf.height    = buffer->height;
    hbuf.stride    = buffer->stride;
    hbuf.format    = buffer->format;
    hbuf.usage     = buffer->usage;
    hbuf.bufferId  = index;
    hbuf.pixelSize = 4;
    hbuf.memHandle = buffer->handle;

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
    sp<ANativeWindow> window;
    ANativeWindowBuffer *abuffer = nullptr;
    {
        std::lock_guard<std::mutex> lock(mLock);
        state = mRequestedState;
        abuffer = mBuffers[buffer.bufferId];
        mBuffers[buffer.bufferId] = nullptr;
        window = mNativeWindow;
    }

    if (abuffer == nullptr) {
        ALOGE ("%s abuffer invalid.\n", __func__);
        return EvsResult::INVALID_ARG;
    }

    if (window != nullptr) {
        window->queueBuffer(window.get(), abuffer, -1);
    }

    // deference buffer.
    abuffer->decStrong(abuffer);

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
