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
#ifndef _FSL_FAKE_CAPTURE_H_
#define _FSL_FAKE_CAPTURE_H_

#include <atomic>
#include <thread>
#include <functional>
#include "EvsCamera.h"

using ::android::hardware::automotive::evs::V1_0::implementation::EvsCamera;

class FakeCapture : public EvsCamera
{
public:
    FakeCapture(const char *deviceName);
    virtual ~FakeCapture();

    virtual bool onOpen(const char* deviceName);
    virtual void onClose();

    virtual bool onStart();
    virtual void onStop();

    virtual bool isOpen();
    // Valid only after open()
    virtual bool onFrameReturn(int index);
    virtual fsl::Memory* onFrameCollect(int &index);
    virtual void onMemoryCreate();

private:
    unsigned int mFrameIndex = 0;
};

#endif
