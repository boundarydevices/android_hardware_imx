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
#include <unordered_set>
#include "EvsCamera.h"

using ::std::condition_variable;
using ::android::hardware::automotive::evs::V1_1::implementation::EvsCamera;
using ::android::hardware::Return;

#define V4L2_BUFFER_NUM 10
class V4l2Capture : public EvsCamera
{
public:
    V4l2Capture(const char *deviceName, const char *videoName,
                   __u32 width, __u32 height, int format,
                  const camera_metadata_t * metadata);
    virtual ~V4l2Capture();

    Return<EvsResult> setMaxFramesInFlight(uint32_t bufferCount);
    virtual bool onOpen(const char* deviceName);
    int onOpenSingleCamera(const char* deviceName);
    virtual void onClose();

    virtual bool onStart();
    virtual void onStop();

    virtual bool isOpen();
    // Valid only after open()
    virtual bool onFrameReturn(int index, std::string deviceid);
    virtual void onFrameCollect(std::vector<struct forwardframe> &frame);
    virtual int getParameter(v4l2_control& control);
    virtual int setParameter(v4l2_control& control);
    virtual std::set<uint32_t>  enumerateCameraControls();
    virtual void onIncreaseMemoryBuffer(unsigned number);
    virtual void onMemoryDestroy();
    void onDecreaseMemoryBuffer(unsigned index);

private:
    int getCaptureMode(int fd, int width, int height);
    int getV4lFormat(int format);
    // mPhysicalCamera return the physical camera
    std::unordered_set<std::string> mPhysicalCamera;

    // if the camera is logic camera, mDeviceFd will been instored according the mPhysicalCamera name.
    // if the camera is pyhsical camera, mDeviceFd will been the fd of pyhsical camera
    std::unordered_map<std::string, int> mDeviceFd;
    __u32 mV4lFormat = 0;
    // judge whether it's logic camera according the metadata
    bool mIslogicCamera;
    unsigned mDecreasenum;
    condition_variable mFramesSignal;
};

#endif
