/*
 *  Copyright 2020 NXP.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#define LOG_TAG "VideoStream"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <log/log.h>

#include "CameraUtils.h"
#include "CameraDeviceSessionHWLImpl.h"
#include "VideoStream.h"
#include "ISPCameraDeviceHWLImpl.h"

namespace android {

VideoStream::VideoStream(CameraDeviceSessionHwlImpl *pSession)
{
    mNumBuffers = 0;
    mOmitFrmCount = 0;
    mOmitFrames = 0;
    mCustomDriver = false;
    mRegistered = false;
    mbStart = false;
    mSession = pSession;
    mRecoverCount = 0;
    memset(mBuffers, 0, sizeof(mBuffers));

    property_get("ro.boot.soc_type", soc_type, "");
}

VideoStream::~VideoStream()
{
}

int32_t VideoStream::openDev(const char* name)
{
    ALOGI("%s", __func__);
    if (name == NULL) {
        ALOGE("invalid dev name");
        return BAD_VALUE;
    }

    //Mutex::Autolock lock(mLock);

    mDev = open(name, O_RDWR | O_NONBLOCK);
    if (mDev <= 0) {
        ALOGE("%s can not open camera devpath:%s", __func__, name);
        return BAD_VALUE;
    }

    return 0;
}

#define CLOSE_WAIT_ITVL_MS 5
#define CLOSE_WAIT_ITVL_US (uint32_t)(CLOSE_WAIT_ITVL_MS*1000)

int32_t VideoStream::closeDev()
{
    ALOGI("%s", __func__);

    if (mDev > 0) {
        close(mDev);
        mDev = -1;
    }

    return 0;
}

int32_t VideoStream::onFlushLocked() {
    int32_t ret = 0;
    struct v4l2_buffer cfilledbuffer;
    struct v4l2_plane planes;
    memset(&planes, 0, sizeof(struct v4l2_plane));

    ALOGI("%s, v4l2 memory type %d, mV4l2BufType %d", __func__, mV4l2MemType, mV4l2BufType);
    // refresh the v4l2 buffers
    for (uint32_t i = 0; i < mNumBuffers; i++) {
        struct v4l2_buffer cfilledbuffer;

        memset(&cfilledbuffer, 0, sizeof(cfilledbuffer));
        cfilledbuffer.memory = mV4l2MemType;
        cfilledbuffer.type = mV4l2BufType;

        if(mV4l2BufType == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
            cfilledbuffer.m.planes = &planes;
            cfilledbuffer.length = 1;
        }

        ret = ioctl(mDev, VIDIOC_DQBUF, &cfilledbuffer);
        if (ret < 0) {
            ALOGE("%s: VIDIOC_DQBUF Failed: %s (%d)", __func__, strerror(errno), errno);
            return BAD_VALUE;
        }
        ret = ioctl(mDev, VIDIOC_QBUF, &cfilledbuffer);
        if (ret < 0) {
            ALOGE("%s: VIDIOC_QBUF Failed: %s (%d)", __func__, strerror(errno), errno);
            return BAD_VALUE;
        }
      }

    return 0;
}


#define ISP_CONTROL "vendor.rw.camera.isp.control"
int32_t VideoStream::ConfigAndStart(uint32_t format, uint32_t width, uint32_t height, uint32_t fps, bool recover)
{
    int ret = 0;

    ALOGI("%s: format 0x%x, res %dx%d, fps %d, recover %d", __func__, format, width, height, fps, recover);

    if (strstr(soc_type, "imx8mq") && (width == 320) && (height == 240)) {
        width = 640;
        height = 480;
        ALOGI("%s, imx8mq, change 240p to 480p", __func__);
    }

    if((mFormat == format) && (mWidth == width) && (mHeight == height) && (mFps == fps) && (recover == false)) {
        ALOGI("%s, same config, format 0x%x, res %dx%d, fps %d", __func__, format, width, height, fps);
        return 0;
    }

    if(mbStart) {
        ret = onDeviceStopLocked();
        if(ret) {
            ALOGE("%s, onDeviceStopLocked failed, ret %d", __func__, ret);
            return ret;
        }

        ret = freeBuffersLocked();
        if(ret) {
            ALOGE("%s, freeBuffersLocked failed, ret %d", __func__, ret);
            return ret;
        }

        if(recover && (strstr(mSession->getSensorData()->camera_name, ISP_SENSOR_NAME))) {
            if (property_set(ISP_CONTROL, "0") < 0)
                ALOGW("%s: property_set %s 0 failed", __func__, ISP_CONTROL);

            if (property_set(ISP_CONTROL, "1") < 0)
                ALOGW("%s: property_set %s 1 failed", __func__, ISP_CONTROL);
        }

        if(recover || (strstr(mSession->getSensorData()->camera_name, ISP_SENSOR_NAME))) {
            closeDev();
            ret = openDev(mSession->getDevPath(0));
            if(ret) {
                ALOGE("%s, openDev %s failed, ret %d", __func__, mSession->getDevPath(0), ret);
                return ret;
            }
        }
    }

    if (strstr(mSession->getSensorData()->camera_name, ISP_SENSOR_NAME)) {
        ((ISPCameraMMAPStream *)this)->getIspWrapper()->init(mDev);

        // Before capture raw data, need first disable DWE.
        if (format == HAL_PIXEL_FORMAT_RAW16)
            ISPProcess(NULL, format);
    }

    ret = onDeviceConfigureLocked(format, width, height, fps);
    if(ret) {
        ALOGE("%s, onDeviceConfigureLocked failed, ret %d", __func__, ret);
        return ret;
    }

    ret = allocateBuffersLocked();
    if (ret) {
        ALOGE("%s: allocateBuffersLocked failed, ret %d", __func__, ret);
        return ret;
    }

    ret = onDeviceStartLocked();
    if (ret) {
        ALOGE("%s: onDeviceStartLocked failed, ret %d", __func__, ret);
        return ret;
    }

    if (strstr(mSession->getSensorData()->camera_name, ISP_SENSOR_NAME)) {
        // get the default dwe para.
        Json::Value jRequest, jResponse;
        ((ISPCameraMMAPStream *)this)->getIspWrapper()->viv_private_ioctl(IF_DWE_G_PARAMS, jRequest, jResponse);
        ((ISPCameraMMAPStream *)this)->getIspWrapper()->parseDewarpParams(jResponse["dwe"]);
    }

    return 0;
}

int32_t VideoStream::Stop()
{
    int ret;

    if(mbStart == false)
        return 0;

    ret = onDeviceStopLocked();
    if(ret) {
        ALOGE("%s, onDeviceStopLocked failed, ret %d", __func__, ret);
        return ret;
    }

    ret = freeBuffersLocked();
    if(ret) {
        ALOGE("%s, freeBuffersLocked failed, ret %d", __func__, ret);
        return ret;
    }

    mWidth = 0;
    mHeight = 0;
    mFps = 0;

    return 0;
}

int32_t VideoStream::postConfigure(uint32_t format, uint32_t width, uint32_t height, uint32_t fps)
{
    mWidth = width;
    mHeight = height;
    mFps = fps;
    mFormat = format;
    mFrames = 0;

    setOmitFrameCount(0);

    struct OmitFrame *item;
    CameraSensorMetadata *pSensorData = mSession->getSensorData();
    struct OmitFrame *mOmitFrame = pSensorData->omit_frame;
    for(item = mOmitFrame; item < mOmitFrame + OMIT_RESOLUTION_NUM; item++) {
        if ((mWidth == item->width) && (mHeight == item->height)) {
            setOmitFrameCount(item->omitnum);
            ALOGI("%s, set omit frames %d for %dx%d", __func__, item->omitnum, mWidth, mHeight);
            break;
        }
    }

    return 0;
}

#define SELECT_TIMEOUT_SECONDS 3
ImxStreamBuffer* VideoStream::onFrameAcquireLocked()
{
    ALOGV("%s", __func__);
    int32_t ret = 0;
    struct v4l2_buffer cfilledbuffer;
    struct v4l2_plane planes;
    memset(&planes, 0, sizeof(struct v4l2_plane));

capture_data:
    memset(&cfilledbuffer, 0, sizeof(cfilledbuffer));

    if (mPlane) {
        cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        cfilledbuffer.m.planes = &planes;
        cfilledbuffer.length = 1;
    } else {
        cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    }

    cfilledbuffer.memory = mV4l2MemType;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(mDev, &fds);
    struct timeval timeout = {0};
    timeout.tv_sec = SELECT_TIMEOUT_SECONDS;
    timeout.tv_usec = 0;

    select(mDev + 1, &fds, NULL, NULL, &timeout);
    if (!FD_ISSET(mDev, &fds)) {
        mRecoverCount++;
        ALOGW("%s: select fd %d blocked %d s on %dx%d, %d fps, camera recover count %d",
            __func__, mDev, SELECT_TIMEOUT_SECONDS, mWidth, mHeight, mFps, mRecoverCount);

        ret = ConfigAndStart(mFormat, mWidth, mHeight, mFps, true);
        if(ret) {
            ALOGE("%s,  ConfigAndStar failed, ret %d", __func__, ret);
            return NULL;
        }

        goto capture_data;
    }

    ret = ioctl(mDev, VIDIOC_DQBUF, &cfilledbuffer);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_DQBUF Failed: %s", __func__, strerror(errno));
        return NULL;
    }

    ALOGV("VIDIOC_DQBUF ok, idx %d", cfilledbuffer.index);

    mFrames++;
    if (mFrames == 1)
        ALOGI("%s: first frame get for %dx%d", __func__, mWidth, mHeight);

    if (mOmitFrames > 0) {
        ALOGI("%s omit frame", __func__);
        ret = ioctl(mDev, VIDIOC_QBUF, &cfilledbuffer);
        if (ret < 0) {
            ALOGE("%s VIDIOC_QBUF Failed", __func__);
            return NULL;
        }
        mOmitFrames--;
        goto capture_data;
    }

    return mBuffers[cfilledbuffer.index];
}

} // namespace android
