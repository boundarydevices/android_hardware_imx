/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
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

#ifndef CAMERA_HAL_H_
#define CAMERA_HAL_H_

#include <cutils/bitops.h>
#include <hardware/hardware.h>
#include <hardware/camera_common.h>
#include <hardware_legacy/uevent.h>
#include <system/camera_vendor_tags.h>
#include "Camera.h"
#include "VendorTags.h"

struct nodeSet {
    char nodeName[CAMERA_SENSOR_LENGTH];
    char devNode[CAMERA_SENSOR_LENGTH];
    nodeSet* next;
};

// CameraHAL contains all module state that isn't specific to an individual
// camera device.
class CameraHAL
{
public:
    CameraHAL();
    ~CameraHAL();

    // Camera Module Interface (see <hardware/camera_common.h>)
    int getNumberOfCameras();
    int getCameraInfo(int camera_id, struct camera_info *info);
    int setCallbacks(const camera_module_callbacks_t *callbacks);
    void getVendorTagOps(vendor_tag_ops_t* ops);

    // Hardware Module Interface (see <hardware/hardware.h>)
    int openDev(const hw_module_t* mod, const char* name, hw_device_t** dev);

private:
    int32_t matchDevNodes();
    int32_t getNodeName(const char* devNode, char name[], size_t length);
    int32_t matchNodeName(const char* nodeName, nodeSet* nodes, int32_t index);
    int32_t matchPropertyName(nodeSet* nodes, int32_t index);

    int32_t handleThreadHotplug();
    void handleThreadExit();
    int32_t handleCameraConnected(char* uevent);
    int32_t handleCameraDisonnected(char* uevent);
    void enumSensorSet();
    void enumSensorNode(int index);

private:
    /**
     * Thread for managing usb camera hotplug. It does below:
     * 1. Monitor camera hotplug status, and notify the status changes by calling
     *    module callback methods.
     * 2. When camera is plugged, create camera device instance, initialize the camera
     *    static info. When camera is unplugged, destroy the camera device instance and
     *    static metadata. As an optimization option, the camera device instance (
     *    including the static info) could be cached when the same camera
     *    plugged/unplugge multiple times.
     */
    class HotplugThread : public android::Thread {
    public:
        HotplugThread(CameraHAL *hal)
            : Thread(false), mModule(hal) {}
        ~HotplugThread() {}

        virtual void onFirstRef() {
            run("HotplugThread", PRIORITY_URGENT_DISPLAY);
        }

        virtual status_t readyToRun(){
            uevent_init();
            return 0;
        }

        // Override below two methods for proper cleanup.
        virtual bool threadLoop() {
            int ret = mModule->handleThreadHotplug();
            if (ret != 0) {
                ALOGI("%s exit...", __func__);
                return false;
            }

            return true;
        }

        virtual void requestExit() {
            // Call parent to set up shutdown
            mModule->handleThreadExit();
            Thread::requestExit();
            // Cleanup other states?
        }

    private:
        CameraHAL *mModule;
    };

private:
    SensorSet mSets[MAX_CAMERAS];
    // Number of cameras
    int32_t mCameraCount;
    // Callback handle
    const camera_module_callbacks_t *mCallbacks;
    // Array of camera devices, contains mCameraCount device pointers
    Camera **mCameras;
    // camera hotplug handle thread.
    sp<HotplugThread> mHotplugThread;
};

#endif // CAMERA_HAL_H_
