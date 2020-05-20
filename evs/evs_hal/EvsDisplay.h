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

#ifndef _FSL_EVS_HW_DISPLAY_H
#define _FSL_EVS_HW_DISPLAY_H

#include <android/hardware/automotive/evs/1.0/IEvsDisplay.h>
#include <nxp/hardware/display/1.0/IDisplay.h>

#include <Memory.h>
#include <MemoryDesc.h>
#include <MemoryManager.h>

namespace android {
namespace hardware {
namespace automotive {
namespace evs {
namespace V1_0 {
namespace implementation {

using ::nxp::hardware::display::V1_0::IDisplay;
#define DISPLAY_BUFFER_NUM 3


class EvsDisplay : public IEvsDisplay {
public:
    // Methods from ::android::hardware::automotive::evs::V1_0::IEvsDisplay follow.
    Return<void> getDisplayInfo(getDisplayInfo_cb _hidl_cb)  override;
    Return<EvsResult> setDisplayState(DisplayState state)  override;
    Return<DisplayState> getDisplayState()  override;
    Return<void> getTargetBuffer(getTargetBuffer_cb _hidl_cb)  override;
    Return<EvsResult> returnTargetBufferForDisplay(const BufferDesc& buffer)  override;

    // Implementation details
    EvsDisplay();
    virtual ~EvsDisplay() override;

    // This gets called if another caller "steals" ownership of the display.
    void forceShutdown();

private:
    bool initialize();
    void showWindow();
    void hideWindow();

private:
    std::mutex mLock;
    int mFormat = 0;
    int mWidth  = 0;
    int mHeight = 0;

    int mIndex = -1;
    fsl::Memory* mBuffers[DISPLAY_BUFFER_NUM] = {};

    DisplayDesc mInfo = {};
    DisplayState mRequestedState = DisplayState::NOT_VISIBLE;

    uint32_t mLayer = -1;
    sp<IDisplay> mDisplay;
};

} // namespace implementation
} // namespace V1_0
} // namespace evs
} // namespace automotive
} // namespace hardware
} // namespace android

#endif  // _FSL_EVS_DISPLAY_H
