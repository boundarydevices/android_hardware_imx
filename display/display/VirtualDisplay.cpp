/*
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

#include "VirtualDisplay.h"

#include <cutils/atomic.h>
#include <cutils/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sync/sync.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

namespace fsl {

VirtualDisplay::VirtualDisplay() {
    mType = DISPLAY_VIRTUAL;
    mBusy = false;
}

VirtualDisplay::~VirtualDisplay() {}

void VirtualDisplay::reset() {
    invalidLayers();

    Mutex::Autolock _l(mLock);

    mRenderTarget = NULL;
    if (mAcquireFence != -1) {
        close(mAcquireFence);
        mAcquireFence = -1;
    }
    mConfigs.clear();
    mActiveConfig = -1;
}

bool VirtualDisplay::busy() {
    Mutex::Autolock _l(mLock);
    return mBusy;
}

void VirtualDisplay::setBusy(bool busy) {
    Mutex::Autolock _l(mLock);
    mBusy = busy;
}

} // namespace fsl
