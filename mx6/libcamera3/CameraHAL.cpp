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

// Default Camera HAL has 2 cameras, front and rear.
static CameraHAL gCameraHAL;
// Handle containing vendor tag functionality
static VendorTags gVendorTags;

CameraHAL::CameraHAL()
  : mNumberOfCameras(0),
    mCallbacks(NULL)
{
    // Allocate camera array and instantiate camera devices
    mCameras = new Camera*[MAX_CAMERAS];

    // Rear camera
    mCameras[0] = createCamera(0, true);
    if (mCameras[0] != NULL) {
        mNumberOfCameras++;
        // Front camera
        mCameras[1] = createCamera(1, false);
        if (mCameras[1] != NULL) {
            mNumberOfCameras++;
        }
    }
}

CameraHAL::~CameraHAL()
{
    for (int32_t i = 0; i < mNumberOfCameras; i++) {
        delete mCameras[i];
    }
    delete [] mCameras;
}

int CameraHAL::getNumberOfCameras()
{
    ALOGV("%s: %d", __func__, mNumberOfCameras);
    return mNumberOfCameras;
}

int CameraHAL::getCameraInfo(int id, struct camera_info* info)
{
    ALOGV("%s: camera id %d: info=%p", __func__, id, info);
    if (id < 0 || id >= mNumberOfCameras) {
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
    } else if (id < 0 || id >= mNumberOfCameras) {
        ALOGE("%s: Invalid camera id %d", __func__, id);
        return -ENODEV;
    }
    return mCameras[id]->openDev(mod, dev);
}

Camera* CameraHAL::createCamera(int32_t id, bool isRear)
{
    char camera_name[CAMERA_SENSOR_LENGTH], camera_prop[PROPERTY_VALUE_MAX];
    char orientStr[CAMERA_SENSOR_LENGTH], orient_prop[PROPERTY_VALUE_MAX];
    char devPath[CAMAERA_FILENAME_LENGTH];
    int32_t facing, orientation;
    bool found = false;

    if (isRear) {
        snprintf(camera_prop, PROPERTY_VALUE_MAX, "%s_%s", "back", FACE_CAMERA_NAME);
        snprintf(orient_prop, PROPERTY_VALUE_MAX, "%s_%s", "back", FACE_CAMERA_ORIENT);
        property_get(camera_prop, camera_name, OV5640MIPI_SENSOR_NAME);
        facing = CAMERA_FACING_BACK;
    }
    else {
        snprintf(camera_prop, PROPERTY_VALUE_MAX, "%s_%s", "front", FACE_CAMERA_NAME);
        snprintf(orient_prop, PROPERTY_VALUE_MAX, "%s_%s", "front", FACE_CAMERA_ORIENT);
        property_get(camera_prop, camera_name, OV5640CSI_SENSOR_NAME);
        facing = CAMERA_FACING_FRONT;
    }
    property_get(orient_prop, orientStr, "0");
    orientation = atoi(orientStr);

    char *pCameraName = strtok(camera_name, ",");
    while (pCameraName != NULL) {
        ALOGI("Checking the camera %s", pCameraName);
        if (getDevPath(pCameraName, devPath, CAMAERA_FILENAME_LENGTH) == -1) {
            pCameraName = strtok(NULL, ",");
            continue;
        }
        ALOGI("Camera ID %d: name %s, Facing %d, orientation %d, dev path %s",
              id, pCameraName, facing, orientation,
              devPath);
        found = true;
        break;
    }

    if (!found) {
        ALOGE("can't find camera id %d, name %s", id, camera_name);
        return NULL;
    }

    return Camera::createCamera(id, pCameraName, facing, orientation, devPath);
}

int32_t CameraHAL::getDevPath(const char* pName, char* pDevPath, uint32_t pathLen)
{
    int  retCode = -1;
    int  fd      = 0;
    char dev_node[CAMAERA_FILENAME_LENGTH];
    DIR *v4l_dir = NULL;
    struct dirent *dir_entry;
    struct v4l2_capability v4l2_cap;
    struct v4l2_dbg_chip_ident vid_chip;

    v4l_dir = opendir("/sys/class/video4linux");
    if (v4l_dir) {
        while ((dir_entry = readdir(v4l_dir))) {
            memset((void *)dev_node, 0, CAMAERA_FILENAME_LENGTH);
            if (strncmp(dir_entry->d_name, "video", 5))
                continue;
            sprintf(dev_node, "/dev/%s", dir_entry->d_name);
            if ((fd = open(dev_node, O_RDWR, O_NONBLOCK)) < 0)
                continue;
            if (ioctl(fd, VIDIOC_QUERYCAP, &v4l2_cap) < 0) {
                close(fd);
                fd = 0;
                continue;
            } else if (v4l2_cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
                if (ioctl(fd, VIDIOC_DBG_G_CHIP_IDENT, &vid_chip) < 0) {
                    if(strstr((const char*)v4l2_cap.driver, pName)) {
                       if (pathLen > strlen(dev_node)) {
                            strcpy(pDevPath, dev_node);
                            ALOGI("Get sensor %s's dev path %s, card %s, driver %s",
                                  pName,
                                  pDevPath,
                                  (const char*)v4l2_cap.card,
                                  (const char*)v4l2_cap.driver);
                            retCode = 0;
                        }
                        close(fd);
                        fd = 0;
                        break;
                    }
                    close(fd);
                    fd = 0;
                    continue;
                }
                if (strstr(vid_chip.match.name, pName)) {
                    // fsl csi/mipi camera name and path match
                    if (pathLen > strlen(dev_node)) {
                        strcpy(pDevPath, dev_node);
                        ALOGI("Get sensor %s's dev path %s",
                              pName,
                              pDevPath);
                        retCode = 0;
                    }
                    close(fd);
                    fd = 0;
                    break;
                }
            }
            close(fd);
            fd = 0;
        }
        closedir(v4l_dir);
    }

    return retCode;
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

