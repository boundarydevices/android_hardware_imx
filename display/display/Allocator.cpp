/*
 * Copyright 2021 NXP.
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

#include "Allocator.h"

#include "IonAllocator.h"
#ifdef ENABLE_DMABUF_HEAP
#include "DmaHeapAllocator.h"
#endif

namespace fsl {

Allocator* Allocator::mInstance(0);
Mutex Allocator::sLock(Mutex::PRIVATE);

Allocator* Allocator::getInstance() {
    Mutex::Autolock _l(sLock);

    if (mInstance != NULL) {
        return mInstance;
    }

#ifdef ENABLE_DMABUF_HEAP
    mInstance = DmaHeapAllocator::getInstance();
#else
    mInstance = IonAllocator::getInstance();
#endif

    return mInstance;
}

} // namespace fsl
