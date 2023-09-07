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
#include <functional>
#include <thread>
#include <unordered_set>

#include "EvsCamera.h"

using ::android::hardware::automotive::evs::V1_1::implementation::EvsCamera;

class FakeCapture : public EvsCamera {
public:
    FakeCapture(const char* deviceName, const camera_metadata_t* metadata);
    virtual ~FakeCapture();

    virtual bool onOpen(const char* deviceName);
    virtual void onClose();

    virtual bool onStart();
    virtual void onStop();

    virtual bool isOpen();
    // Valid only after open()
    virtual bool onFrameReturn(int index, std::string deviceid);
    virtual void onFrameCollect(std::vector<struct forwardframe>& frame);
    virtual int getParameter(v4l2_control& control);
    virtual int setParameter(v4l2_control& control);
    virtual void onIncreaseMemoryBuffer(unsigned number);
    virtual void onMemoryDestroy();
    virtual std::set<uint32_t> enumerateCameraControls();
    void readFromPng(const char* file, void* buf);

private:
    std::unordered_set<std::string> mPhysicalCamera;
    bool mIslogicCamera;
    // if the camera is logic camera, mDeviceFd will been instored according the mPhysicalCamera
    // name. if the camera is pyhsical camera, mDeviceFd will been the fd of pyhsical camera
    std::unordered_map<std::string, int> mDeviceFd;
};

#endif
