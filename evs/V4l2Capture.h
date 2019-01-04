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
#ifndef _FSL_V4L2_CAPTURE_H_
#define _FSL_V4L2_CAPTURE_H_

#include <atomic>
#include <thread>
#include <functional>
#include <linux/videodev2.h>
#include "EvsCamera.h"

using ::android::hardware::automotive::evs::V1_0::implementation::EvsCamera;

class V4l2Capture : public EvsCamera
{
public:
    V4l2Capture(const char *deviceName);
    virtual ~V4l2Capture();

    virtual bool onOpen(const char* deviceName);
    virtual void onClose();

    virtual bool onStart();
    virtual void onStop();

    virtual bool isOpen();
    // Valid only after open()
    virtual bool onFrameReturn(int index);
    virtual fsl::Memory* onFrameCollect(int &index);

private:
    int getCaptureMode(int fd, int width, int height);
    int getV4lFormat(int format);

    int mDeviceFd = -1;
    __u32 mV4lFormat = 0;
};

#endif
