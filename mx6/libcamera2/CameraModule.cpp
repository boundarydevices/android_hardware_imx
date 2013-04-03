/*
 * Copyright (C) Freescale - http://www.Freescale.com/
 * Copyright (C) 2012-2013 Freescale Semiconductor, Inc.
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

#define LOG_TAG "CameraHAL"
#include <linux/videodev2.h>
#include <linux/mxcfb.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <dirent.h>
#include <utils/threads.h>
#include <cutils/properties.h>
#include "CameraHal.h"
#include "CameraUtil.h"

#define MAX_CAMERAS_SUPPORTED 2

static android::Mutex gCameraHalDeviceLock;

static int camera_device_open(const hw_module_t *module,
                              const char        *name,
                              hw_device_t      **device);
static int camera_device_close(hw_device_t *device);
static int camera_get_number_of_cameras(void);
static int camera_get_camera_info(int camera_id,
                                  struct camera_info *info);

static struct hw_module_methods_t camera_module_methods = {
    open: camera_device_open
};

camera_module_t HAL_MODULE_INFO_SYM = {
    common: {
        tag: HARDWARE_MODULE_TAG,
        module_api_version: CAMERA_MODULE_API_VERSION_2_0,
        hal_api_version: HARDWARE_HAL_API_VERSION,
        id: CAMERA_HARDWARE_MODULE_ID,
        name: "Freescale CameraHal Module",
        author: "Freescale",
        methods: &camera_module_methods,
        dso: NULL,       /* remove compilation warnings */
        reserved: { 0 }, /* remove compilation warnings */
    },
    get_number_of_cameras: camera_get_number_of_cameras,
    get_camera_info: camera_get_camera_info,
};

typedef struct fsl_camera_device {
    camera2_device_t base;
    CameraHal *camHal;
    int cameraid;
} fsl_camera_device_t;

CameraHal *fsl_get_camerahal(const camera2_device_t *device)
{
    CameraHal *camHal = NULL;
    if (device && device->priv) {
        fsl_camera_device_t *fslDev = (fsl_camera_device_t *)device->priv;
        camHal = fslDev->camHal;
    }

    return camHal;
}

/*******************************************************************
* implementation of camera2_device_ops functions
*******************************************************************/
int set_request_queue_src_ops(const struct camera2_device *device,
        const camera2_request_queue_src_ops_t *request_src_ops)
{
    int ret = INVALID_OPERATION;
    CameraHal *camHal = fsl_get_camerahal(device);

    if (camHal != NULL) {
        ret = camHal->set_request_queue_src_ops(request_src_ops);
    }
    return ret;
}

int notify_request_queue_not_empty(const struct camera2_device *device)
{
    int ret = INVALID_OPERATION;
    CameraHal *camHal = fsl_get_camerahal(device);

    if (camHal != NULL) {
        ret = camHal->notify_request_queue_not_empty();
    }
    return ret;
}

int set_frame_queue_dst_ops(const struct camera2_device *device,
        const camera2_frame_queue_dst_ops_t *frame_dst_ops)
{
    int ret = INVALID_OPERATION;
    CameraHal *camHal = fsl_get_camerahal(device);

    if (camHal != NULL) {
        ret = camHal->set_frame_queue_dst_ops(frame_dst_ops);
    }
    return ret;
}

int get_in_progress_count(const struct camera2_device *device)
{
    int ret = INVALID_OPERATION;
    CameraHal *camHal = fsl_get_camerahal(device);

    if (camHal != NULL) {
        ret = camHal->get_in_progress_count();
    }
    return ret;
}

int flush_captures_in_progress(const struct camera2_device *)
{
    FLOGE("%s:does not support now",__FUNCTION__);
    return INVALID_OPERATION;
}

int construct_default_request(const struct camera2_device *device,
    int request_template,
    camera_metadata_t **request)
{
    int ret = INVALID_OPERATION;
    CameraHal *camHal = fsl_get_camerahal(device);

    if (camHal != NULL) {
        ret = camHal->construct_default_request(request_template, request);
    }
    return ret;
}

int allocate_stream(const struct camera2_device *device,
        uint32_t width,
        uint32_t height,
        int      format,
        const camera2_stream_ops_t *stream_ops,
        uint32_t *stream_id,
        uint32_t *format_actual,
        uint32_t *usage,
        uint32_t *max_buffers)
{
    int ret = INVALID_OPERATION;
    CameraHal *camHal = fsl_get_camerahal(device);

    if (camHal != NULL) {
        ret = camHal->allocate_stream(width, height, format, stream_ops,
            stream_id, format_actual, usage, max_buffers);
    }
    return ret;
}

int register_stream_buffers(
        const struct camera2_device *device,
        uint32_t stream_id,
        int num_buffers,
        buffer_handle_t *buffers)
{
    int ret = INVALID_OPERATION;
    CameraHal *camHal = fsl_get_camerahal(device);

    if (camHal != NULL) {
        ret = camHal->register_stream_buffers(stream_id, num_buffers, buffers);
    }
    return ret;
}

int release_stream(
        const struct camera2_device *device,
        uint32_t stream_id)
{
    int ret = INVALID_OPERATION;
    CameraHal *camHal = fsl_get_camerahal(device);

    if (camHal != NULL) {
        ret = camHal->release_stream(stream_id);
    }
    return ret;
}

int allocate_reprocess_stream(const struct camera2_device *,
        uint32_t width,
        uint32_t height,
        uint32_t format,
        const camera2_stream_in_ops_t *reprocess_stream_ops,
        uint32_t *stream_id,
        uint32_t *consumer_usage,
        uint32_t *max_buffers)
{
    return INVALID_OPERATION;
}

int allocate_reprocess_stream_from_stream(const struct camera2_device *,
        uint32_t output_stream_id,
        const camera2_stream_in_ops_t *reprocess_stream_ops,
        uint32_t *stream_id)
{
    return INVALID_OPERATION;
}

int release_reprocess_stream(
        const struct camera2_device *,
        uint32_t stream_id)
{
    return INVALID_OPERATION;
}

int trigger_action(const struct camera2_device *,
        uint32_t trigger_id,
        int32_t ext1,
        int32_t ext2)
{
    return INVALID_OPERATION;
}

int set_notify_callback(const struct camera2_device *device,
        camera2_notify_callback notify_cb,
        void *user)
{
    int ret = INVALID_OPERATION;
    CameraHal *camHal = fsl_get_camerahal(device);

    if (camHal != NULL) {
        ret = camHal->set_notify_callback(notify_cb, user);
    }
    return ret;
}

int get_metadata_vendor_tag_ops(const struct camera2_device *device,
        vendor_tag_query_ops_t **ops)
{
    int ret = INVALID_OPERATION;
    CameraHal *camHal = fsl_get_camerahal(device);

    if (camHal != NULL) {
        ret = camHal->get_metadata_vendor_tag_ops(ops);
    }
    return ret;
}

int camera_dump(const struct camera2_device *, int fd)
{
    return INVALID_OPERATION;
}

camera2_device_ops_t fsl_camera_ops = {
    set_request_queue_src_ops:           set_request_queue_src_ops,
    notify_request_queue_not_empty:      notify_request_queue_not_empty,
    set_frame_queue_dst_ops:             set_frame_queue_dst_ops,
    get_in_progress_count:               get_in_progress_count,
    flush_captures_in_progress:          flush_captures_in_progress,
    construct_default_request:           construct_default_request,

    allocate_stream:                     allocate_stream,
    register_stream_buffers:             register_stream_buffers,
    release_stream:                      release_stream,

    allocate_reprocess_stream:           allocate_reprocess_stream,
    allocate_reprocess_stream_from_stream: allocate_reprocess_stream_from_stream,
    release_reprocess_stream:            release_reprocess_stream,

    trigger_action:                      trigger_action,
    set_notify_callback:                 set_notify_callback,
    get_metadata_vendor_tag_ops:         get_metadata_vendor_tag_ops,
    dump:                                camera_dump,
};

int camera_device_close(hw_device_t *device)
{
    int ret = 0;
    fsl_camera_device_t *fsl_dev = NULL;
    CameraHal *camHal = NULL;

    ALOGV("%s", __FUNCTION__);

    android::Mutex::Autolock lock(gCameraHalDeviceLock);

    if (!device) {
        return -EINVAL;
    }

    camera2_device_t *camDev = (camera2_device_t *)device;
    camHal = fsl_get_camerahal(camDev);
    fsl_dev = (fsl_camera_device_t *)camDev->priv;

    if (camHal) {
        delete camHal;
    }

    if (fsl_dev) {
        free(fsl_dev);
    }


    return ret;
}

#define FACE_BACK_CAMERA_NAME "back_camera_name"
#define FACE_FRONT_CAMERA_NAME "front_camera_name"
#define FACE_BACK_CAMERA_ORIENT "back_camera_orient"
#define FACE_FRONT_CAMERA_ORIENT "front_camera_orient"
#define DEFAULT_ERROR_NAME '0'
#define DEFAULT_ERROR_NAME_str "0"
#define UVC_NAME "uvc"
static struct CameraInfo sCameraInfo[2];
static int gCameraNum = 0;

/*******************************************************************
* implementation of camera_module functions
*******************************************************************/

/* open device handle to one of the cameras
 *
 * assume camera service will keep singleton of each camera
 * so this function will always only be called once per camera instance
 */
int camera_device_open(const hw_module_t *module,
                       const char        *name,
                       hw_device_t      **device)
{
    int rv          = 0;
    int num_cameras = 0;
    int cameraid;
    fsl_camera_device_t *camera_device = NULL;
    CameraHal *camera                  = NULL;
    char *SelectedCameraName;

    android::Mutex::Autolock lock(gCameraHalDeviceLock);

    ALOGI("camera_device open: %s", name);

    if (name != NULL) {
        cameraid    = atoi(name);
        num_cameras = camera_get_number_of_cameras();

        if (cameraid > num_cameras)
        {
            ALOGE("camera service provided cameraid out of bounds, "
                  "cameraid = %d, num supported = %d",
                  cameraid, num_cameras);
            rv = -EINVAL;
            goto fail;
        }

        camera_device = (fsl_camera_device_t *)malloc(sizeof(*camera_device));
        if (!camera_device)
        {
            ALOGE("camera_device allocation fail");
            rv = -ENOMEM;
            goto fail;
        }

        memset(camera_device, 0, sizeof(*camera_device));

        camera_device->base.common.tag     = HARDWARE_DEVICE_TAG;
        camera_device->base.common.version = CAMERA_DEVICE_API_VERSION_2_0;
        camera_device->base.common.module  = (hw_module_t *)(module);
        camera_device->base.common.close   = camera_device_close;
        camera_device->base.ops            = &fsl_camera_ops;
        camera_device->base.priv           = camera_device;

        *device = &camera_device->base.common;

        camera_device->cameraid = cameraid;

        camera = new CameraHal(cameraid);

        if (!camera)
        {
            ALOGE("Couldn't create instance of CameraHal class");
            rv = -ENOMEM;
            goto fail;
        }

        camera_device->camHal = camera;
        if (camera->initialize(sCameraInfo[cameraid]) < 0) {
            rv = -EINVAL;
            goto fail;
        }
    }

    return rv;

fail:
    if (camera_device) {
        free(camera_device);
        camera_device = NULL;
    }
    if (camera) {
        delete camera;
        camera = NULL;
    }
    *device = NULL;
    return rv;
}

int GetDevPath(const char  *pCameraName,
               char        *pCameraDevPath,
               unsigned int pathLen)
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
                    if(strstr((const char*)v4l2_cap.driver, pCameraName)) {
                       if (pathLen > strlen(dev_node)) {
                            strcpy(pCameraDevPath, dev_node);
                            ALOGI("Get sensor %s's dev path %s",
                                  pCameraName,
                                  pCameraDevPath);
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
                if (strstr(vid_chip.match.name, pCameraName)) {
                    // fsl csi/mipi camera name and path match
                    if (pathLen > strlen(dev_node)) {
                        strcpy(pCameraDevPath, dev_node);
                        ALOGI("Get sensor %s's dev path %s",
                              pCameraName,
                              pCameraDevPath);
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

static void GetCameraPropery(char *pFaceBackCameraName,
                             char *pFaceFrontCameraName,
                             int  *pFaceBackOrient,
                             int  *pFaceFrontOrient)
{
    char orientStr[10];

    property_get(FACE_BACK_CAMERA_NAME,
                 pFaceBackCameraName,
                 DEFAULT_ERROR_NAME_str);
    property_get(FACE_BACK_CAMERA_ORIENT, orientStr, DEFAULT_ERROR_NAME_str);

    if (orientStr[0] == DEFAULT_ERROR_NAME)
        *pFaceBackOrient = 0;
    else
        *pFaceBackOrient = atoi(orientStr);

    ALOGI("Face Back Camera is %s, orient is %d",
          pFaceBackCameraName,
          *pFaceBackOrient);

    property_get(FACE_FRONT_CAMERA_NAME,
                 pFaceFrontCameraName,
                 DEFAULT_ERROR_NAME_str);

    property_get(FACE_FRONT_CAMERA_ORIENT, orientStr, DEFAULT_ERROR_NAME_str);


    if (orientStr[0] == DEFAULT_ERROR_NAME)
        *pFaceFrontOrient = 0;
    else
        *pFaceFrontOrient = atoi(orientStr);

    ALOGI("Face Front Camera is %s, orient is %d",
          pFaceFrontCameraName,
          *pFaceFrontOrient);
}

int camera_get_number_of_cameras()
{
    int back_orient = 0,  front_orient = 0;
    int numCamera = 0;

    if (gCameraNum == 0) {
        char name_back[CAMERA_SENSOR_LENGTH];
        char name_front[CAMERA_SENSOR_LENGTH];
        GetCameraPropery(name_back,
                         name_front,
                         &back_orient,
                         &front_orient);
        if (name_back[0] != DEFAULT_ERROR_NAME) {
            char *pCameraName = strtok(name_back, ",");
            while (pCameraName != NULL) {
                ALOGI("Checking the camera %s", pCameraName);
                strncpy(sCameraInfo[gCameraNum].name,
                        pCameraName,
                        CAMERA_SENSOR_LENGTH);
                sCameraInfo[gCameraNum].facing      = CAMERA_FACING_BACK;
                sCameraInfo[gCameraNum].orientation = back_orient;
                memset(sCameraInfo[gCameraNum].devPath, 0, CAMAERA_FILENAME_LENGTH);
                if (GetDevPath(sCameraInfo[gCameraNum].name,
                           sCameraInfo[gCameraNum].devPath,
                           CAMAERA_FILENAME_LENGTH) == -1){
                    pCameraName = strtok(NULL, ",");
                    continue;
                }
                ALOGI("Camera ID %d: name %s, Facing %d, orientation %d, dev path %s",
                        gCameraNum,
                        sCameraInfo[gCameraNum].name,
                        sCameraInfo[gCameraNum].facing,
                        sCameraInfo[gCameraNum].orientation,
                        sCameraInfo[gCameraNum].devPath);
                gCameraNum++;
                break;
            }
            if (gCameraNum == 0) {
                if (strstr(name_back, UVC_NAME)) {
                    strncpy(sCameraInfo[gCameraNum].name, UVC_NAME,
                            CAMERA_SENSOR_LENGTH);
                    gCameraNum++;
                }
            }
        }
        numCamera = gCameraNum;
        if (name_front[0] != DEFAULT_ERROR_NAME) {
            char *pCameraName = strtok(name_front, ",");
            while (pCameraName != NULL) {
                ALOGI("Checking the camera %s", pCameraName);
                strncpy(sCameraInfo[gCameraNum].name,
                        pCameraName,
                        CAMERA_SENSOR_LENGTH);
                sCameraInfo[gCameraNum].facing      = CAMERA_FACING_FRONT;
                sCameraInfo[gCameraNum].orientation = front_orient;
                memset(sCameraInfo[gCameraNum].devPath, 0, CAMAERA_FILENAME_LENGTH);
                if (GetDevPath(sCameraInfo[gCameraNum].name,
                       sCameraInfo[gCameraNum].devPath,
                       CAMAERA_FILENAME_LENGTH) == -1) {
                    pCameraName = strtok(NULL, ",");
                    continue;
                }
                ALOGI("Camera ID %d: name %s, Facing %d, orientation %d, dev path %s",
                    gCameraNum,
                    sCameraInfo[gCameraNum].name,
                    sCameraInfo[gCameraNum].facing,
                    sCameraInfo[gCameraNum].orientation,
                    sCameraInfo[gCameraNum].devPath);
                gCameraNum++;
                break;
            }
            if (gCameraNum == numCamera) {
                if (strstr(name_front, UVC_NAME)) {
                    strncpy(sCameraInfo[gCameraNum].name, UVC_NAME,
                            CAMERA_SENSOR_LENGTH);
                    gCameraNum++;
                }
            }
        }
    }
    return gCameraNum;
}

int camera_get_camera_info(int                 cameraId,
                           struct camera_info *cameraInfo)
{
    //MetadaManager::createStaticInfo(cameraId, &sCameraInfo[cameraId]);
    sCameraInfo[cameraId].device_version = HARDWARE_DEVICE_API_VERSION(2, 0);
    memcpy(cameraInfo, &sCameraInfo[cameraId], sizeof(camera_info));
    return 0;
}

