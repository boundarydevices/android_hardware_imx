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

#ifndef FSL_GRAPHICS_DISPLAY_H
#define FSL_GRAPHICS_DISPLAY_H

#include <nxp/hardware/display/1.0/IDisplay.h>

#include <mutex>

#include "Layer.h"

namespace nxp {
namespace hardware {
namespace display {
namespace V1_0 {
namespace implementation {

using ::android::Rect;
using ::android::Region;
using ::android::sp;
using ::android::hardware::hidl_handle;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::nxp::hardware::display::V1_0::Error;

class DisplayHal : public IDisplay {
public:
    // Methods from ::android::hardware::graphics::display::V1_0::IDisplay follow.
    Return<void> getLayer(uint32_t count, getLayer_cb _hidl_cb) override;
    Return<Error> putLayer(uint32_t layer) override;
    Return<void> getSlot(uint32_t layer, getSlot_cb _hidl_cb) override;
    Return<Error> presentLayer(uint32_t layer, uint32_t slot, const hidl_handle& buffer) override;
    Return<void> setSecureDisplayEnable(bool enable, uint32_t x, uint32_t y, uint32_t w,
                                        uint32_t h) override;

    // Dump apis
    Return<void> debug(const hidl_handle& fd, const hidl_vec<hidl_string>& args) override;
    void cmdDump(int fd, const hidl_vec<hidl_string>& options);
    void cmdHelp(int fd);
    void cmdList(int fd, const hidl_vec<hidl_string>& options);
    void cmdDumpDevice(int fd, const hidl_vec<hidl_string>& options);

private:
    std::mutex mLock;
    fsl::Layer* mLayers[MAX_LAYERS] = {};
};

} // namespace implementation
} // namespace V1_0
} // namespace display
} // namespace hardware
} // namespace nxp

#endif // FSL_GRAPHICS_DISPLAY_H
