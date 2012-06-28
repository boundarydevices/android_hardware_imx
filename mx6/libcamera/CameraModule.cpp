/*
 * Copyright (C) Freescale - http://www.Freescale.com/
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

/**
* @file CameraHal.cpp
*
* This file maps the Camera Hardware Interface to V4L2.
*
*/

#define LOG_TAG "CameraHAL"

#include <utils/threads.h>
#include <cutils/properties.h>
#include "CameraHal.h"
#include "Camera_utils.h"

#define MAX_CAMERAS_SUPPORTED 2
//static android::CameraProperties gCameraProperties;
static android::CameraHal* gCameraHals[MAX_CAMERAS_SUPPORTED];
static unsigned int gCamerasOpen = 0;
static android::Mutex gCameraHalDeviceLock;

static int camera_device_open(const hw_module_t* module, const char* name,
                hw_device_t** device);
static int camera_device_close(hw_device_t* device);
static int camera_get_number_of_cameras(void);
static int camera_get_camera_info(int camera_id, struct camera_info *info);

static struct hw_module_methods_t camera_module_methods = {
        open: camera_device_open
};

camera_module_t HAL_MODULE_INFO_SYM = {
    common: {
         tag: HARDWARE_MODULE_TAG,
         version_major: 1,
         version_minor: 0,
         id: CAMERA_HARDWARE_MODULE_ID,
         name: "Freescale CameraHal Module",
         author: "Freescale",
         methods: &camera_module_methods,
         dso: NULL, /* remove compilation warnings */
         reserved: {0}, /* remove compilation warnings */
    },
    get_number_of_cameras: camera_get_number_of_cameras,
    get_camera_info: camera_get_camera_info,
};

typedef struct fsl_camera_device {
    camera_device_t base;
    int cameraid;
} fsl_camera_device_t;


/*******************************************************************
 * implementation of camera_device_ops functions
 *******************************************************************/

int camera_set_preview_window(struct camera_device * device,
        struct preview_stream_ops *window)
{
    int rv = -EINVAL;
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return rv;

    fsl_dev = (fsl_camera_device_t*) device;

    rv = gCameraHals[fsl_dev->cameraid]->setPreviewWindow(window);

    return rv;
}

void camera_set_callbacks(struct camera_device * device,
        camera_notify_callback notify_cb,
        camera_data_callback data_cb,
        camera_data_timestamp_callback data_cb_timestamp,
        camera_request_memory get_memory,
        void *user)
{
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return;

    fsl_dev = (fsl_camera_device_t*) device;

    gCameraHals[fsl_dev->cameraid]->setCallbacks(notify_cb, data_cb, data_cb_timestamp, get_memory, user);
}

void camera_enable_msg_type(struct camera_device * device, int32_t msg_type)
{
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return;

    fsl_dev = (fsl_camera_device_t*) device;

    gCameraHals[fsl_dev->cameraid]->enableMsgType(msg_type);
}

void camera_disable_msg_type(struct camera_device * device, int32_t msg_type)
{
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return;

    fsl_dev = (fsl_camera_device_t*) device;

    gCameraHals[fsl_dev->cameraid]->disableMsgType(msg_type);
}

int camera_msg_type_enabled(struct camera_device * device, int32_t msg_type)
{
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return 0;

    fsl_dev = (fsl_camera_device_t*) device;

    return gCameraHals[fsl_dev->cameraid]->msgTypeEnabled(msg_type);
}

int camera_start_preview(struct camera_device * device)
{
    int rv = -EINVAL;
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return rv;

    fsl_dev = (fsl_camera_device_t*) device;

    rv = gCameraHals[fsl_dev->cameraid]->startPreview();

    return rv;
}

void camera_stop_preview(struct camera_device * device)
{
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return;

    fsl_dev = (fsl_camera_device_t*) device;

    gCameraHals[fsl_dev->cameraid]->stopPreview();
}

int camera_preview_enabled(struct camera_device * device)
{
    int rv = -EINVAL;
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return rv;

    fsl_dev = (fsl_camera_device_t*) device;

    rv = gCameraHals[fsl_dev->cameraid]->previewEnabled();
    return rv;
}

int camera_store_meta_data_in_buffers(struct camera_device * device, int enable)
{
    int rv = -EINVAL;
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return rv;

    fsl_dev = (fsl_camera_device_t*) device;

    //  TODO: meta data buffer not current supported
    rv = gCameraHals[fsl_dev->cameraid]->storeMetaDataInBuffers(enable);
    return rv;
    //return enable ? android::INVALID_OPERATION: android::OK;
}

int camera_start_recording(struct camera_device * device)
{
    int rv = -EINVAL;
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return rv;

    fsl_dev = (fsl_camera_device_t*) device;

    rv = gCameraHals[fsl_dev->cameraid]->startRecording();
    return rv;
}

void camera_stop_recording(struct camera_device * device)
{
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return;

    fsl_dev = (fsl_camera_device_t*) device;

    gCameraHals[fsl_dev->cameraid]->stopRecording();
}

int camera_recording_enabled(struct camera_device * device)
{
    int rv = -EINVAL;
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return rv;

    fsl_dev = (fsl_camera_device_t*) device;

    rv = gCameraHals[fsl_dev->cameraid]->recordingEnabled();
    return rv;
}

void camera_release_recording_frame(struct camera_device * device,
                const void *opaque)
{
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return;

    fsl_dev = (fsl_camera_device_t*) device;

    gCameraHals[fsl_dev->cameraid]->releaseRecordingFrame(opaque);
}

int camera_auto_focus(struct camera_device * device)
{
    int rv = -EINVAL;
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return rv;

    fsl_dev = (fsl_camera_device_t*) device;

    rv = gCameraHals[fsl_dev->cameraid]->autoFocus();
    return rv;
}

int camera_cancel_auto_focus(struct camera_device * device)
{
    int rv = -EINVAL;
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return rv;

    fsl_dev = (fsl_camera_device_t*) device;

    rv = gCameraHals[fsl_dev->cameraid]->cancelAutoFocus();
    return rv;
}

int camera_take_picture(struct camera_device * device)
{
    int rv = -EINVAL;
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return rv;

    fsl_dev = (fsl_camera_device_t*) device;

    rv = gCameraHals[fsl_dev->cameraid]->takePicture();
    return rv;
}

int camera_cancel_picture(struct camera_device * device)
{
    int rv = -EINVAL;
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return rv;

    fsl_dev = (fsl_camera_device_t*) device;

    rv = gCameraHals[fsl_dev->cameraid]->cancelPicture();
    return rv;
}

int camera_set_parameters(struct camera_device * device, const char *params)
{
    int rv = -EINVAL;
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return rv;

    fsl_dev = (fsl_camera_device_t*) device;

    rv = gCameraHals[fsl_dev->cameraid]->setParameters(params);
    return rv;
}

char* camera_get_parameters(struct camera_device * device)
{
    char* param = NULL;
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return NULL;

    fsl_dev = (fsl_camera_device_t*) device;

    param = gCameraHals[fsl_dev->cameraid]->getParameters();

    return param;
}

static void camera_put_parameters(struct camera_device *device, char *parms)
{
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return;

    fsl_dev = (fsl_camera_device_t*) device;

    gCameraHals[fsl_dev->cameraid]->putParameters(parms);
}

int camera_send_command(struct camera_device * device,
            int32_t cmd, int32_t arg1, int32_t arg2)
{
    int rv = -EINVAL;
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return rv;

    fsl_dev = (fsl_camera_device_t*) device;

    rv = gCameraHals[fsl_dev->cameraid]->sendCommand(cmd, arg1, arg2);
    return rv;
}

void camera_release(struct camera_device * device)
{
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    if(!device)
        return;

    fsl_dev = (fsl_camera_device_t*) device;

    gCameraHals[fsl_dev->cameraid]->release();
}

int camera_dump(struct camera_device * device, int fd)
{
    int rv = -EINVAL;
    fsl_camera_device_t* fsl_dev = NULL;

    if(!device)
        return rv;

    fsl_dev = (fsl_camera_device_t*) device;

    rv = gCameraHals[fsl_dev->cameraid]->dump(fd);
    return rv;
}

extern "C" void heaptracker_free_leaked_memory(void);

int camera_device_close(hw_device_t* device)
{
    int ret = 0;
    fsl_camera_device_t* fsl_dev = NULL;

    LOGV("%s", __FUNCTION__);

    android::Mutex::Autolock lock(gCameraHalDeviceLock);

    if (!device) {
        ret = -EINVAL;
        goto done;
    }

    fsl_dev = (fsl_camera_device_t*) device;

    if (fsl_dev) {
        if (gCameraHals[fsl_dev->cameraid]) {
            delete gCameraHals[fsl_dev->cameraid];
            gCameraHals[fsl_dev->cameraid] = NULL;
            gCamerasOpen--;
        }

        if (fsl_dev->base.ops) {
            free(fsl_dev->base.ops);
        }
        free(fsl_dev);
    }
done:
#ifdef HEAPTRACKER
    heaptracker_free_leaked_memory();
#endif
    return ret;
}

#define FACE_BACK_CAMERA_NAME "back_camera_name"
#define FACE_FRONT_CAMERA_NAME "front_camera_name"
#define FACE_BACK_CAMERA_ORIENT "back_camera_orient"
#define FACE_FRONT_CAMERA_ORIENT "front_camera_orient"
#define DEFAULT_ERROR_NAME '#'
#define DEFAULT_ERROR_NAME_str "#"
#define UVC_NAME "uvc"
static struct camera_info sCameraInfo[2];
static char Camera_name[2][MAX_SENSOR_NAME];
/*******************************************************************
 * implementation of camera_module functions
 *******************************************************************/

/* open device handle to one of the cameras
 *
 * assume camera service will keep singleton of each camera
 * so this function will always only be called once per camera instance
 */

int camera_device_open(const hw_module_t* module, const char* name,
                hw_device_t** device)
{
    int rv = 0;
    int num_cameras = 0;
    int cameraid;
    fsl_camera_device_t* camera_device = NULL;
    camera_device_ops_t* camera_ops = NULL;
    android::CameraHal* camera = NULL;
    char *SelectedCameraName;
    android::sp<android::CaptureDeviceInterface> pCaptureDevice = NULL;
    android::sp<android::PostProcessDeviceInterface> pPPDevice = NULL;
    android::sp<android::JpegEncoderInterface>pJpegEncoder = NULL;
    //android::CameraProperties::Properties* properties = NULL;

    android::Mutex::Autolock lock(gCameraHalDeviceLock);

    LOGI("camera_device open");

    if (name != NULL) {
        cameraid = atoi(name);
        num_cameras = camera_get_number_of_cameras();//gCameraProperties.camerasSupported();

        if(cameraid > num_cameras)
        {
            LOGE("camera service provided cameraid out of bounds, "
                    "cameraid = %d, num supported = %d",
                    cameraid, num_cameras);
            rv = -EINVAL;
            goto fail;
        }
#if 0
        if(gCamerasOpen >= MAX_SIMUL_CAMERAS_SUPPORTED)
        {
            LOGE("maximum number of cameras already open");
            rv = -ENOMEM;
            goto fail;
        }
#endif
        camera_device = (fsl_camera_device_t*)malloc(sizeof(*camera_device));
        if(!camera_device)
        {
            LOGE("camera_device allocation fail");
            rv = -ENOMEM;
            goto fail;
        }

        camera_ops = (camera_device_ops_t*)malloc(sizeof(*camera_ops));
        if(!camera_ops)
        {
            LOGE("camera_ops allocation fail");
            rv = -ENOMEM;
            goto fail;
        }

        memset(camera_device, 0, sizeof(*camera_device));
        memset(camera_ops, 0, sizeof(*camera_ops));

        camera_device->base.common.tag = HARDWARE_DEVICE_TAG;
        camera_device->base.common.version = 0;
        camera_device->base.common.module = (hw_module_t *)(module);
        camera_device->base.common.close = camera_device_close;
        camera_device->base.ops = camera_ops;

        camera_ops->set_preview_window = camera_set_preview_window;
        camera_ops->set_callbacks = camera_set_callbacks;
        camera_ops->enable_msg_type = camera_enable_msg_type;
        camera_ops->disable_msg_type = camera_disable_msg_type;
        camera_ops->msg_type_enabled = camera_msg_type_enabled;
        camera_ops->start_preview = camera_start_preview;
        camera_ops->stop_preview = camera_stop_preview;
        camera_ops->preview_enabled = camera_preview_enabled;
        camera_ops->store_meta_data_in_buffers = camera_store_meta_data_in_buffers;
        camera_ops->start_recording = camera_start_recording;
        camera_ops->stop_recording = camera_stop_recording;
        camera_ops->recording_enabled = camera_recording_enabled;
        camera_ops->release_recording_frame = camera_release_recording_frame;
        camera_ops->auto_focus = camera_auto_focus;
        camera_ops->cancel_auto_focus = camera_cancel_auto_focus;
        camera_ops->take_picture = camera_take_picture;
        camera_ops->cancel_picture = camera_cancel_picture;
        camera_ops->set_parameters = camera_set_parameters;
        camera_ops->get_parameters = camera_get_parameters;
        camera_ops->put_parameters = camera_put_parameters;
        camera_ops->send_command = camera_send_command;
        camera_ops->release = camera_release;
        camera_ops->dump = camera_dump;

        *device = &camera_device->base.common;

        camera_device->cameraid = cameraid;
        SelectedCameraName = Camera_name[sCameraInfo[cameraid].facing];

        pCaptureDevice = android::createCaptureDevice(SelectedCameraName);
        pPPDevice = android::createPPDevice();
        pJpegEncoder = android::createJpegEncoder(android::SOFTWARE_JPEG_ENC);

        camera = new android::CameraHal(cameraid);

        if(!camera)
        {
            LOGE("Couldn't create instance of CameraHal class");
            rv = -ENOMEM;
            goto fail;
        }

        if (camera->setCaptureDevice(pCaptureDevice) < 0 ||
                camera->setPostProcessDevice(pPPDevice) < 0 ||
                camera->setJpegEncoder(pJpegEncoder) < 0)
            return NULL;

        if (camera->Init() < 0)
            return NULL;

        //now the board has only one csi camera sensor, so just do mirror for it
        if(strstr(SelectedCameraName, "ov") != NULL){
            camera->setPreviewRotate(android::CAMERA_PREVIEW_HORIZ_FLIP);
        }

        gCameraHals[cameraid] = camera;
        gCamerasOpen++;
    }

    return rv;

fail:
    if(camera_device) {
        free(camera_device);
        camera_device = NULL;
    }
    if(camera_ops) {
        free(camera_ops);
        camera_ops = NULL;
    }
    if(camera) {
        delete camera;
        camera = NULL;
    }
    *device = NULL;
    return rv;
}

static void GetCameraPropery(char * pFaceBackCameraName, char *pFaceFrontCameraName, int *pFaceBackOrient, int *pFaceFrontOrient)
{
    char orientStr[10];

    property_get (FACE_BACK_CAMERA_NAME, pFaceBackCameraName, DEFAULT_ERROR_NAME_str );
    property_get (FACE_BACK_CAMERA_ORIENT, orientStr, DEFAULT_ERROR_NAME_str );

    if (orientStr[0] == DEFAULT_ERROR_NAME )
        *pFaceBackOrient = 0;
    else 
        *pFaceBackOrient = atoi(orientStr);

    LOGI("Face Back Camera is %s, orient is %d", pFaceBackCameraName, *pFaceBackOrient);

    property_get(FACE_FRONT_CAMERA_NAME, pFaceFrontCameraName, DEFAULT_ERROR_NAME_str );

    property_get(FACE_FRONT_CAMERA_ORIENT, orientStr, DEFAULT_ERROR_NAME_str );


    if (orientStr[0] == DEFAULT_ERROR_NAME )
        *pFaceFrontOrient = 0;
    else 
        *pFaceFrontOrient = atoi(orientStr);

    LOGI("Face Front Camera is %s, orient is %d", pFaceFrontCameraName, *pFaceFrontOrient);

}

int camera_get_number_of_cameras()
{
    int back_orient =0,  front_orient = 0;
    int back_camera_num = 0, front_camera_num = 0;
    GetCameraPropery(Camera_name[0], Camera_name[1], &back_orient, &front_orient);
    if (Camera_name[0][0] != DEFAULT_ERROR_NAME){
        sCameraInfo[0].facing = CAMERA_FACING_BACK;
        sCameraInfo[0].orientation = back_orient;
        back_camera_num++;
    }
    if (Camera_name[1][0] != DEFAULT_ERROR_NAME){
        if(back_camera_num > 0){
            sCameraInfo[1].facing = CAMERA_FACING_FRONT;
            sCameraInfo[1].orientation = front_orient;
        }else{
	    sCameraInfo[0].facing = CAMERA_FACING_FRONT;
	    sCameraInfo[0].orientation = front_orient;
        }
        front_camera_num ++;
    }
    return (back_camera_num + front_camera_num);					

}

int camera_get_camera_info(int cameraId, struct camera_info* cameraInfo)
{
    memcpy(cameraInfo, &sCameraInfo[cameraId], sizeof(camera_info));
    return 0;
}

