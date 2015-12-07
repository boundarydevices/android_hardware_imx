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

#include <cstdlib>
#include <stdint.h>
#include <sys/types.h>
#include <hardware/camera_common.h>
#include <hardware/hardware.h>
#include <sys/stat.h>
#include <linux/videodev2.h>
#include <dirent.h>
#include "VendorTags.h"

//#define LOG_NDEBUG 0
#include <cutils/log.h>

#define ATRACE_TAG (ATRACE_TAG_CAMERA | ATRACE_TAG_HAL)
#include <cutils/trace.h>

#include "CameraHAL.h"

/* Hardware limitation on I.MX6DQ platform
 * VPU only support NV12&I420 format.
 * IPU doesn't support NV21 format.
 * But android framework requires NV21&YV12 format support.
 * YV12&I420 Y/UV stride doesn't match between android framework and IPU/GPU.
     ** Android YV12&I420 define:
     * - a horizontal stride multiple of 16 pixels
     * - a vertical stride equal to the height
     * - y_size = stride * height
     * - c_stride = ALIGN(stride/2, 16)
     *
     ** GPU YV12&I420 limitation:
     * - GPU limit Y stride to be 32 alignment, and UV stride 16 alignment.
     *
     ** IPU hardware YV12&I420 limitation:
     * - IPU limit the Y stride to be 2x of the UV stride alignment.
     ** IPU driver YV12&I420 define:
     * - y_stride = width
     * - uv_stride = y_stride / 2;
 * So there is work around to treat the format on I.MX6DQ platform:
 * Change format NV21&YV12 to NV12&I420 in Camera framework.
 * The NV21 format required by CTS is treated as NV12.
 * YUV alignment required by CTS doesn't match on I.MX6DQ platform.
 */

/*
 * This file serves as the entry point to the HAL.  It contains the module
 * structure and functions used by the framework to load and interface to this
 * HAL, as well as the handles to the individual camera devices.
 */
#define BACK_CAMERA_ID 0
#define FRONT_CAMERA_ID 1
#define CAMERA_PLUG_EVENT "video4linux/video"
#define CAMERA_PLUG_ADD "add@"
#define CAMERA_PLUG_REMOVE "remove@"
// Default Camera HAL has 2 cameras, front and rear.
static CameraHAL gCameraHAL;
// Handle containing vendor tag functionality
static VendorTags gVendorTags;

/** Note:
 * camera sensors are configured in init.rc.
 * camera count depends on camera configuration.
 * Back and Front camera both support hotplug when configured as USB Camera.
 */

CameraHAL::CameraHAL()
  : mCameraCount(0),
    mCallbacks(NULL)
{
    // Allocate camera array and instantiate camera devices
    mCameras = new Camera*[MAX_CAMERAS];
    memset(mSets, 0, sizeof(mSets));
    memset(mCameras, 0, MAX_CAMERAS * sizeof(Camera*));

    // enumerate all camera sensors.
    enumSensorSet();

    // check if camera exists.
    for (int32_t index=0; index<MAX_CAMERAS; index++) {
        if (!mSets[index].mExisting) {
            // count on usb camera if set in init.rc.
            if (strstr(mSets[index].mPropertyName, UVC_SENSOR_NAME)) {
                mCameraCount++;
            }
            continue;
        }
        mCameras[index] = Camera::createCamera(index, mSets[index].mSensorName,
                                mSets[index].mFacing, mSets[index].mOrientation,
                                mSets[index].mDevPath);

        if (mCameras[index] == NULL) {
            // camera sensor is not supported now.
            // So, camera count should not change.
            ALOGW("Error: camera:%d, %s create failed", index,
                                 mSets[index].mSensorName);
        }
        else {
            mCameraCount++;
        }
    }
    ALOGI("camera number is %d", mCameraCount);

    mHotplugThread = new HotplugThread(this);
}

CameraHAL::~CameraHAL()
{
    for (int32_t i = 0; i < mCameraCount; i++) {
        if (mCameras[i] != NULL) {
            delete mCameras[i];
        }
    }
    delete [] mCameras;
}

int32_t CameraHAL::handleCameraConnected(char* uevent)
{
    size_t nameLen = CAMERA_SENSOR_LENGTH - 1;
    char* devName = strstr(uevent, "video");
    char* tmpName = devName;
    if (devName == NULL) {
        ALOGW("%s bad uevent:%s", __func__, uevent);
        return 0;
    }

    while (devName != NULL) {
        tmpName = devName;
        devName = strstr(devName+1, "video");
    }

    nodeSet* node = (nodeSet*)malloc(sizeof(nodeSet));
    if (node == NULL) {
        ALOGE("%s malloc failed", __func__);
        return 0;
    }

    memset(node, 0, sizeof(nodeSet));
    sprintf(node->devNode, "/dev/%s", tmpName);

    // usb camera is not ready until 100ms.
    usleep(100000);
    getNodeName(node->devNode, node->nodeName, nameLen);
    ALOGI("%s devNode:%s, nodeName:%s", __func__, node->devNode, node->nodeName);

    for (int32_t index = 0; index < mCameraCount; index++) {
        // sensor is absent then to check it.
        if (!mSets[index].mExisting) {
            // match sensor according to property set.
            matchPropertyName(node, index);
            if (!mSets[index].mExisting) {
                ALOGI("sensor plug event:%s enumerate failed", uevent);
                continue;
            }

            mCameras[index] = Camera::createCamera(index,
                    mSets[index].mSensorName, mSets[index].mFacing,
                    mSets[index].mOrientation, mSets[index].mDevPath);
            if (mCameras[index] == NULL) {
                // camera sensor is not supported now.
                ALOGE("Error: camera %s, %s create failed",
                        mSets[index].mSensorName, uevent);
                return 0;
            }

            struct camera_info info;
            //get camera static information.
            mCameras[index]->getInfo(&info);
            //notify framework camera status changed.
            mCallbacks->camera_device_status_change(mCallbacks, index,
                    CAMERA_DEVICE_STATUS_PRESENT);
            return 0;
        }
    }

    return 0;
}

int32_t CameraHAL::handleCameraDisonnected(char* uevent)
{
    char* devPath = NULL;
    for (int32_t index = 0; index < mCameraCount; index++) {
        devPath = strstr(mSets[index].mDevPath, "video");
        if ((devPath != NULL) && strstr(uevent, devPath)) {
            if (mCameras[index] == NULL) {
                ALOGW("camera:%d disconnected but without object", index);
                mSets[index].mExisting = false;
                return 0;
            }

            // only camera support hotplug can be removed.
            if (!mCameras[index]->isHotplug()) {
                ALOGW("camera %d, name:%s on board can't disconnect",
                        index, mSets[index].mSensorName);
                return 0;
            }

            // notify framework camera status changed.
            mCallbacks->camera_device_status_change(mCallbacks, index,
                    CAMERA_DEVICE_STATUS_NOT_PRESENT);
            delete mCameras[index];
            mCameras[index] = NULL;
            mSets[index].mExisting = false;
        }
    }

    return 0;
}

int32_t CameraHAL::handleThreadHotplug()
{
    /**
     * check camera connection status change, if connected, do below:
     * 1. create camera device, add to mCameras.
     * 2. init static info (mCameras[id]->initstaticinfo())
     * 3. notify on_status_change callback
     *
     * if unconnected, similarly, do below:
     * 1. destroy camera device and remove it from mCameras.
     * 2. notify on_status_change callback
     *
     * do not have a tight polling loop here, to avoid excessive cpu utilization.
     */
    char uevent_desc[4096];
    memset(uevent_desc, 0, sizeof(uevent_desc));
    int len = uevent_next_event(uevent_desc, sizeof(uevent_desc) - 2);
    if (strstr(uevent_desc, CAMERA_PLUG_EVENT) != NULL) {
        ALOGI("%s uevent %s", __func__, uevent_desc);
        if (strstr(uevent_desc, CAMERA_PLUG_ADD) != NULL) {
            // handle camera add event.
            handleCameraConnected(uevent_desc);
        }
        else if (strstr(uevent_desc, CAMERA_PLUG_REMOVE) != NULL) {
            //handle camera remove event.
            handleCameraDisonnected(uevent_desc);
        }
        else {
            ALOGI("%s doesn't handle uevent %s", __func__, uevent_desc);
        }
    }

    return 0;
}

void CameraHAL::handleThreadExit()
{
}

int CameraHAL::getNumberOfCameras()
{
    ALOGV("%s: %d", __func__, mCameraCount);
    return mCameraCount;
}

int CameraHAL::getCameraInfo(int id, struct camera_info* info)
{
    ALOGI("%s: camera id %d: info=%p", __func__, id, info);
    if ((id < 0) || (id >= mCameraCount) || (mCameras[id] == NULL)) {
        ALOGE("%s: Invalid camera id %d", __func__, id);
        return -ENODEV;
    }
    // TODO: return device-specific static metadata
    return mCameras[id]->getInfo(info);
}

int CameraHAL::setCallbacks(const camera_module_callbacks_t *callbacks)
{
    ALOGV("%s : callbacks=%p", __func__, callbacks);
    mCallbacks = callbacks;
    return 0;
}

int CameraHAL::openDev(const hw_module_t* mod, const char* name, hw_device_t** dev)
{
    int id;
    char *nameEnd;

    ALOGV("%s: module=%p, name=%s, device=%p", __func__, mod, name, dev);
    if (*name == '\0') {
        ALOGE("%s: Invalid camera id name is NULL", __func__);
        return -EINVAL;
    }
    id = strtol(name, &nameEnd, 10);
    if (*nameEnd != '\0') {
        ALOGE("%s: Invalid camera id name %s", __func__, name);
        return -EINVAL;
    } else if (id < 0 || id >= mCameraCount) {
        ALOGE("%s: Invalid camera id %d", __func__, id);
        return -ENODEV;
    }
    return mCameras[id]->openDev(mod, dev);
}

void CameraHAL::enumSensorSet()
{
    // get property from init.rc mSets.
    char orientStr[CAMERA_SENSOR_LENGTH];
    char *pCameraName = NULL;
    int32_t ret = 0;

    ALOGI("%s", __func__);
    // get back camera property.
    memset(orientStr, 0, sizeof(orientStr));
    property_get("back_camera_name", mSets[BACK_CAMERA_ID].mPropertyName, "0");
    property_get("back_camera_orient", orientStr, "0");
    mSets[BACK_CAMERA_ID].mOrientation = atoi(orientStr);
    mSets[BACK_CAMERA_ID].mFacing = CAMERA_FACING_BACK;
    mSets[BACK_CAMERA_ID].mExisting = false;

    // get front camera property.
    memset(orientStr, 0, sizeof(orientStr));
    property_get("front_camera_name", mSets[FRONT_CAMERA_ID].mPropertyName, "0");
    property_get("front_camera_orient", orientStr, "0");
    mSets[FRONT_CAMERA_ID].mOrientation = atoi(orientStr);
    mSets[FRONT_CAMERA_ID].mFacing = CAMERA_FACING_FRONT;
    mSets[FRONT_CAMERA_ID].mExisting = false;

    // make sure of back&front camera parameters.
    matchDevNodes();
}

int32_t CameraHAL::matchPropertyName(nodeSet* nodes, int32_t index)
{
    char nodeName[32];
    char *propName = NULL;
    char *pNextName = NULL;
    int32_t ret = 0;

    if (nodes == NULL) {
        return 0;
    }

    propName = mSets[index].mPropertyName;
    ALOGI("matchPropertyName: index:%d, %s", index, propName);
    while (propName != NULL && (strlen(propName) > 0) && *propName != '0') {
        memset(nodeName, 0, sizeof(nodeName));
        pNextName = strchr(propName, ',');
        if (pNextName == NULL) {
            strncpy(nodeName, propName, 32);
            propName = pNextName;
        }
        else {
            strncpy(nodeName, propName, pNextName - propName);
            propName = pNextName + 1;
        }

        ALOGI("index:%d, propName:%s", index, nodeName);

        ret = matchNodeName(nodeName, nodes, index);
        if (ret != 0) {
            continue;
        }

        break;
    }

    return ret;
}

int32_t CameraHAL::matchNodeName(const char* nodeName, nodeSet* nodes, int32_t index)
{
    if (nodes == NULL) {
        return -1;
    }

    const char* sensorName = NULL;
    const char* devNode = NULL;
    int32_t ret = -1;

    ALOGI("%s", __func__);
    nodeSet* node = nodes;
    while (node != NULL) {
        devNode = node->devNode;
        sensorName = node->nodeName;
        if (strlen(sensorName) == 0) {
            node = node->next;
            continue;
        }

        ALOGI("matchNodeName: sensor:%s, dev:%s, node:%s, index:%d",
              sensorName, devNode, nodeName, index);
        if (!strstr(sensorName, nodeName)) {
            node = node->next;
            continue;
        }

        strncpy(mSets[index].mSensorName, nodeName, PROPERTY_VALUE_MAX);
        strncpy(mSets[index].mDevPath, devNode, CAMAERA_FILENAME_LENGTH);
        ALOGI("Camera ID %d: name %s, Facing %d, orientation %d, dev path %s",
                index, mSets[index].mSensorName, mSets[index].mFacing,
                mSets[index].mOrientation, mSets[index].mDevPath);
        mSets[index].mExisting = true;
        ret = 0;
        break;
    }

    return ret;
}

int32_t CameraHAL::matchDevNodes()
{
    DIR *vidDir = NULL;
    struct dirent *dirEntry;
    size_t nameLen = CAMERA_SENSOR_LENGTH - 1;
    nodeSet *nodes = NULL, *node = NULL, *last = NULL;

    ALOGI("%s", __func__);
    vidDir = opendir("/sys/class/video4linux");
    if (vidDir == NULL) {
        return -1;
    }

    while ((dirEntry = readdir(vidDir)) != NULL) {
        if (strncmp(dirEntry->d_name, "video", 5)) {
            continue;
        }

        node = (nodeSet*)malloc(sizeof(nodeSet));
        if (node == NULL) {
            ALOGE("%s malloc failed", __func__);
            break;
        }

        memset(node, 0, sizeof(nodeSet));
        if (nodes == NULL) {
            nodes = node;
        }
        else {
            last->next = node;
        }
        last = node;

        sprintf(node->devNode, "/dev/%s", dirEntry->d_name);

        getNodeName(node->devNode, node->nodeName, nameLen);
    }

    closedir(vidDir);

    for (int32_t index=0; index<MAX_CAMERAS; index++) {
        matchPropertyName(nodes, index);
    }

    return 0;
}

int32_t CameraHAL::getNodeName(const char* devNode, char name[], size_t length)
{
    int32_t ret = -1;
    int32_t fd = -1;
    size_t strLen = 0;
    struct v4l2_capability vidCap;
    struct v4l2_dbg_chip_ident vidChip;

    ALOGI("getNodeName: dev path:%s", devNode);
    if ((fd = open(devNode, O_RDWR, O_NONBLOCK)) < 0) {
        ALOGW("%s open dev path:%s failed:%s", __func__, devNode,
                strerror(errno));
        return ret;
    }

    ret = ioctl(fd, VIDIOC_QUERYCAP, &vidCap);
    if (ret < 0) {
        ALOGW("%s QUERYCAP dev path:%s failed", __func__, devNode);
        close(fd);
        fd = -1;
        return ret;
    }

    if (!(vidCap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        ALOGW("%s dev path:%s is not capture", __func__, devNode);
        close(fd);
        fd = -1;
        ret = -1;
        return ret;
    }

    strncat(name, (const char*)vidCap.driver, length);
    strLen = strlen((const char*)vidCap.driver);
    length -= strLen;
    ALOGI("getNodeName: node name:%s", name);

    ret = ioctl(fd, VIDIOC_DBG_G_CHIP_IDENT, &vidChip);
    if (ret < 0) {
        ALOGW("%s CHIP_IDENT dev path:%s failed", __func__, devNode);
        close(fd);
        fd = -1;
        return ret;
    }

    strncat(name, ",", length);
    strLen = 1;
    length -= strLen;
    strncat(name, vidChip.match.name, length);

    ALOGI("getNodeNames: node name:%s", name);
    close(fd);

    return ret;
}

extern "C" {

static int get_number_of_cameras()
{
    return gCameraHAL.getNumberOfCameras();
}

static int get_camera_info(int id, struct camera_info* info)
{
    return gCameraHAL.getCameraInfo(id, info);
}

static int set_callbacks(const camera_module_callbacks_t *callbacks)
{
    return gCameraHAL.setCallbacks(callbacks);
}

static int get_tag_count(const vendor_tag_ops_t* ops)
{
    return gVendorTags.getTagCount(ops);
}

static void get_all_tags(const vendor_tag_ops_t* ops, uint32_t* tag_array)
{
    gVendorTags.getAllTags(ops, tag_array);
}

static const char* get_section_name(const vendor_tag_ops_t* ops, uint32_t tag)
{
    return gVendorTags.getSectionName(ops, tag);
}

static const char* get_tag_name(const vendor_tag_ops_t* ops, uint32_t tag)
{
    return gVendorTags.getTagName(ops, tag);
}

static int get_tag_type(const vendor_tag_ops_t* ops, uint32_t tag)
{
    return gVendorTags.getTagType(ops, tag);
}

static void get_vendor_tag_ops(vendor_tag_ops_t* ops)
{
    ALOGV("%s : ops=%p", __func__, ops);
    ops->get_tag_count      = get_tag_count;
    ops->get_all_tags       = get_all_tags;
    ops->get_section_name   = get_section_name;
    ops->get_tag_name       = get_tag_name;
    ops->get_tag_type       = get_tag_type;
}

static int open_dev(const hw_module_t* mod, const char* name, hw_device_t** dev)
{
    return gCameraHAL.openDev(mod, name, dev);
}

static hw_module_methods_t gCameraModuleMethods = {
    open : open_dev
};

camera_module_t HAL_MODULE_INFO_SYM __attribute__ ((visibility("default"))) = {
    common : {
        tag                : HARDWARE_MODULE_TAG,
        module_api_version : CAMERA_MODULE_API_VERSION_2_2,
        hal_api_version    : HARDWARE_HAL_API_VERSION,
        id                 : CAMERA_HARDWARE_MODULE_ID,
        name               : "Default Camera HAL",
        author             : "The Android Open Source Project",
        methods            : &gCameraModuleMethods,
        dso                : NULL,
        reserved           : {0},
    },
    get_number_of_cameras : get_number_of_cameras,
    get_camera_info       : get_camera_info,
    set_callbacks         : set_callbacks,
    get_vendor_tag_ops    : get_vendor_tag_ops,
    open_legacy           : NULL,
    set_torch_mode        : NULL,
    init                  : NULL,
    reserved              : {0},
};
} // extern "C"

