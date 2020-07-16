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

#include <inttypes.h>
#include <Memory.h>
#include <MemoryDesc.h>
#include <DisplayManager.h>
#include <Display.h>
#include <sync/sync.h>

#include "DisplayHal.h"

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <android-base/stringprintf.h>
#include <android-base/file.h>
#include <android-base/unique_fd.h>

namespace nxp {
namespace hardware {
namespace display {
namespace V1_0 {
namespace implementation {

#define MAIN_DISPLAY 0

using ::android::base::EqualsIgnoreCase;
using ::android::base::StringPrintf;
using ::android::base::WriteStringToFd;

// Methods from ::android::hardware::graphics::display::V1_0::IDisplay follow.
Return<void> DisplayHal::getLayer(uint32_t count, getLayer_cb _hidl_cb)
{
    uint32_t layer = -1;

    // create hw layer and buffer slot.
    {
        std::lock_guard<std::mutex> lock(mLock);
        for (uint32_t i=0; i<MAX_LAYERS; i++) {
            if (mLayers[i] != nullptr) {
                continue;
            }

            mLayers[i] = new fsl::Layer();
            mLayers[i]->index = i;
            layer = i;

            fsl::BufferSlot* buffer = new fsl::BufferSlot(count);
            mLayers[i]->flags = fsl::BUFFER_SLOT;
            mLayers[i]->priv = buffer;
            ALOGI("%s flags:0x%x, priv:%p", __func__, mLayers[i]->flags, mLayers[i]->priv);
            break;
        }
    }

    if (layer == (uint32_t)-1) {
        ALOGE("%s no free slot", __func__);
        _hidl_cb(Error::NO_RESOURCES, layer);
        return Void();
    }

    ALOGI("%s index:%d", __func__, layer);
    _hidl_cb(Error::NONE, layer);
    return Void();
}

Return<Error> DisplayHal::putLayer(uint32_t layer)
{
    ALOGI("%s index:%d", __func__, layer);

    if (layer >= MAX_LAYERS) {
        ALOGE("%s invalid layer", __func__);
        return Error::BAD_VALUE;
    }

    // remove hw layer from display.
    fsl::Display* pDisplay = NULL;
    fsl::DisplayManager* displayManager = fsl::DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(MAIN_DISPLAY);
    if (pDisplay == NULL) {
        ALOGE("%s get main display failed", __func__);
    }
    else {
        pDisplay->removeHwLayer(layer);
    }

    // release hw layer.
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (mLayers[layer] == nullptr) {
            ALOGE("%s layer not exists", __func__);
            return Error::BAD_VALUE;
        }

        delete (fsl::BufferSlot*)mLayers[layer]->priv;
        delete mLayers[layer];
        mLayers[layer] = nullptr;
    }

    // refresh gui back.
    fsl::EventListener* callback = NULL;
    callback = displayManager->getCallback();
    if (callback != NULL) {
        callback->onRefresh(0);
    }

    return Error::NONE;
}

Return<void> DisplayHal::getSlot(uint32_t layer, getSlot_cb _hidl_cb)
{
    uint32_t slot = -1;
    if (layer >= MAX_LAYERS) {
        ALOGE("%s invalid layer", __func__);
        _hidl_cb(Error::BAD_VALUE, slot);
        return Void();
    }

    // find the hw layer.
    fsl::Layer* pLayer = nullptr;
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (mLayers[layer] == nullptr) {
            ALOGE("%s layer not exists", __func__);
            _hidl_cb(Error::BAD_VALUE, slot);
            return Void();
        }

        pLayer = mLayers[layer];
    }

    // check the hw layer property.
    fsl::BufferSlot *queue = (fsl::BufferSlot *)pLayer->priv;
    if (!(pLayer->flags & fsl::BUFFER_SLOT) || queue == nullptr) {
        ALOGE("%s flags:0x%x, queue:%p", __func__, pLayer->flags, queue);
        ALOGE("%s layer without BUFFER_SLOT flag", __func__);
        _hidl_cb(Error::BAD_VALUE, slot);
        return Void();
    }

    // get buffer slot.
    slot = queue->getFreeSlot();
    _hidl_cb(Error::NONE, slot);
    return Void();
}

Return<Error> DisplayHal::presentLayer(uint32_t layer, uint32_t slot,
              const hidl_handle& buffer)
{
    if (layer >= MAX_LAYERS) {
        ALOGE("%s invalid layer", __func__);
        return Error::BAD_VALUE;
    }

    fsl::Layer* pLayer = nullptr;
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (mLayers[layer] == nullptr) {
            ALOGE("%s layer not exists", __func__);
            return Error::BAD_VALUE;
        }

        pLayer = mLayers[layer];
    }

    fsl::BufferSlot *queue = (fsl::BufferSlot *)pLayer->priv;
    if (!(pLayer->flags & fsl::BUFFER_SLOT) || queue == nullptr) {
        ALOGE("%s layer without BUFFER_SLOT flag", __func__);
        return Error::BAD_VALUE;
    }

    const fsl::Memory *handle = (const fsl::Memory *)buffer.getNativeHandle();

    fsl::Display* pDisplay = NULL;
    fsl::DisplayManager* displayManager = fsl::DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(MAIN_DISPLAY);
    if (pDisplay == NULL) {
        ALOGE("%s get main display failed", __func__);
        return Error::NO_RESOURCES;
    }

    if (!pLayer->busy) {
        pLayer->transform = 0;
        pLayer->blendMode = fsl::BLENDING_PREMULT;
        pLayer->planeAlpha = 255;
        pLayer->color = 0;
        // set source crop to buffer size.
        Rect &src = pLayer->sourceCrop;
        src.left = src.top = 0;
        src.right = handle->width;
        src.bottom = handle->height;

        // set display frame to display resolution.
        const fsl::DisplayConfig& config = pDisplay->getActiveConfig();
        Rect &dst = pLayer->displayFrame;
        dst.left = dst.top = 0;
        dst.right = config.mXres;
        dst.bottom = config.mYres;
        pLayer->visibleRegion.set(dst);
        pLayer->acquireFence = -1;
        pLayer->releaseFence = -1;
        // set zorder to max value.
        pLayer->zorder = 0x7fffffff;
        pLayer->origType = fsl::LAYER_TYPE_DEVICE;
        pLayer->busy = true;
        pDisplay->addHwLayer(layer, pLayer);
    }

    queue->addPresentSlot(slot, (fsl::Memory *)handle);

    if (pDisplay->triggerComposition()) {
        pDisplay->composeLayers();
        pDisplay->updateScreen();
        int presentFence = -1;
        pDisplay->getPresentFence(&presentFence);
        if (presentFence != -1) {
            sync_wait(presentFence, -1);
            close(presentFence);
        }

        return Error::NONE;
    }

    //For the first frame, vync maybe disabled
    //Menually trigger the onRefresh for composer
    if(queue->presentTotal() == 1) {
        fsl::EventListener* callback = NULL;
        callback = displayManager->getCallback();
        if (callback != NULL) {
             callback->onRefresh(0);
        }
    }

    return Error::NONE;
}

Return<void> DisplayHal::debug(const hidl_handle& fd , const hidl_vec<hidl_string>& options) {
    if (fd.getNativeHandle() != nullptr && fd->numFds > 0) {
        cmdDump(fd->data[0], options);
    } else {
        LOG(ERROR) << "Given file descriptor is not valid.";
    }

    return {};
}

void DisplayHal::cmdDump(int fd, const hidl_vec<hidl_string>& options) {
    if (options.size() == 0) {
        WriteStringToFd("No option is given.\n", fd);
        cmdHelp(fd);
        return;
    }

    const std::string option = options[0];
    if (EqualsIgnoreCase(option, "--help")) {
        cmdHelp(fd);
    } else if (EqualsIgnoreCase(option, "--list")) {
        cmdList(fd, options);
    } else if (EqualsIgnoreCase(option, "--dump")) {
        cmdDumpDevice(fd, options);
    } else {
        WriteStringToFd(StringPrintf("Invalid option: %s\n", option.c_str()),fd);
        cmdHelp(fd);
    }
}

void DisplayHal::cmdHelp(int fd) {
    WriteStringToFd("--help: shows this help.\n"
                    "--list: [option1|option2|...|all]: lists all the dump options: option1 or option2 or ... or all\n"
                    "available to Display Hal.\n"
                    "--dump option1: shows current status of the option1\n"
                    "--dump option2: shows current status of the option2\n"
                    "--dump all: shows current status of all the options\n", fd);
    return;
}

void DisplayHal::cmdList(int fd, const hidl_vec<hidl_string>& options) {
    bool listoption1 = false;
    bool listoption2 = false;
    if (options.size() > 1) {
        const std::string option = options[1];
        const bool listAll = EqualsIgnoreCase(option, "all");
        listoption1 = listAll || EqualsIgnoreCase(option, "option1");
        listoption2 = listAll || EqualsIgnoreCase(option, "option2");
        if (!listoption1 && !listoption2) {
            WriteStringToFd(StringPrintf("Unrecognized option is ignored.\n\n"),fd);
            cmdHelp(fd);
            return;
        }
        if(listoption1) {
            WriteStringToFd(StringPrintf("list option1 dump options, default is --list listoption1.\n"),fd);
         }

        if(listoption2) {
            WriteStringToFd(StringPrintf("list option2 dump options, default is --list listoption2.\n"),fd);
        }
    } else {
        WriteStringToFd(StringPrintf("Invalid input, need to append list option.\n\n"),fd);
        cmdHelp(fd);
     }
}

void DisplayHal::cmdDumpDevice(int fd, const hidl_vec<hidl_string>& options) {
    bool listoption1 = false;
    bool listoption2 = false;
    if (options.size() > 1) {
        const std::string option = options[1];
        const bool listAll = EqualsIgnoreCase(option, "all");
        listoption1 = listAll || EqualsIgnoreCase(option, "option1");
        listoption2 = listAll || EqualsIgnoreCase(option, "option2");
        if (!listoption1 && !listoption2) {
            WriteStringToFd(StringPrintf("Unrecognized option is ignored.\n\n"),fd);
            cmdHelp(fd);
            return;
        }
        if(listoption1) {
            WriteStringToFd(StringPrintf("dump option1 info.\n"),fd);
        }
        if(listoption2) {
            WriteStringToFd(StringPrintf("dump option2 info.\n"),fd);
        }
    } else {
        WriteStringToFd(StringPrintf("Invalid input, need to append dump option.\n\n"),fd);
        cmdHelp(fd);
    }
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace display
}  // namespace hardware
}  // namespace nxp
