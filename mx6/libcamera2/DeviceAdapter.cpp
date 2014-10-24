/*
 * Copyright (C) 2012-2014 Freescale Semiconductor, Inc.
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

#include "DeviceAdapter.h"
#include "UvcDevice.h"
#include "UvcMJPGDevice.h"
#include "Ov5640Mipi.h"
#include "Ov5642Csi.h"
#include "Ov5640Csi.h"
#include "TVINDevice.h"
#include <stdlib.h>

#define DEFAULT_ERROR_NAME '0'
#define DEFAULT_ERROR_NAME_str "0"

sp<DeviceAdapter>DeviceAdapter::Create(const CameraInfo& info)
{
    sp<DeviceAdapter> devAdapter;

    if (strstr(info.name, UVC_SENSOR_NAME)) {
        char uvcMJPGStr[92];
        int configUseMJPG = 0;

        property_get(UVC_USE_MJPG, uvcMJPGStr, DEFAULT_ERROR_NAME_str);

        if (uvcMJPGStr[0] == DEFAULT_ERROR_NAME)
            configUseMJPG = 0;
        else
            configUseMJPG = atoi(uvcMJPGStr);

        if(configUseMJPG == 0) {
            FLOGI("DeviceAdapter: Create uvc device");
            devAdapter = new UvcDevice();
        } else {
            FLOGI("DeviceAdapter: Create uvc device, config to use MJPG");
            devAdapter = new UvcMJPGDevice();
        }
    }
    else if (strstr(info.name, OV5640MIPI_SENSOR_NAME)) {
        FLOGI("DeviceAdapter: Create ov5640 mipi device");
        devAdapter = new Ov5640Mipi();
    }
    else if (strstr(info.name, OV5642CSI_SENSOR_NAME)) {
        FLOGI("DeviceAdapter: Create ov5642 csi device");
        devAdapter = new Ov5642Csi();
    }
    else if (strstr(info.name, OV5640CSI_SENSOR_NAME)) {
        FLOGI("DeviceAdapter: Create ov5640 csi device");
        devAdapter = new Ov5640Csi();
    }
    else if (strstr(info.name, ADV7180_TVIN_NAME)) {
        FLOGI("DeviceAdapter: Create adv7180 device");
        devAdapter = new TVINDevice();
    }
    else {
        devAdapter = new OvDevice();
        FLOGI("sensor %s does not support well now!", info.name);
    }

    return devAdapter;
}

DeviceAdapter::DeviceAdapter()
    : mVideoInfo(NULL), mCameraHandle(-1), mQueued(0), mCpuNum(0)
{}

DeviceAdapter::~DeviceAdapter()
{
    // Close the camera handle and free the video info structure
    if (mCameraHandle > 0) {
        close(mCameraHandle);
        mCameraHandle = -1;
    }

    if (mVideoInfo) {
        delete mVideoInfo;
        mVideoInfo = NULL;
    }
}

void DeviceAdapter::setMetadaManager(sp<MetadaManager> &metadaManager)
{
    mMetadaManager = metadaManager;
}

PixelFormat DeviceAdapter::getMatchFormat(int *sfmt, int  slen,
                                         int *dfmt, int  dlen)
{
    if ((sfmt == NULL) || (slen == 0) || (dfmt == NULL) || (dlen == 0)) {
        FLOGE("getMatchFormat invalid parameters");
        return 0;
    }

    PixelFormat matchFormat = 0;
    bool live = true;
    for (int i = 0; i < slen && live; i++) {
        for (int j = 0; j < dlen; j++) {
            if (sfmt[i] == dfmt[j]) {
                matchFormat = dfmt[j];
                live        = false;
                break;
            }
        }
    }

    return matchFormat;
}

void DeviceAdapter::setPreviewPixelFormat()
{
    int vpuFormats[MAX_VPU_SUPPORT_FORMAT];
    memset(vpuFormats, 0, sizeof(vpuFormats));
    int ret = mMetadaManager->getSupportedRecordingFormat(&vpuFormats[0],
                                    MAX_VPU_SUPPORT_FORMAT);
    if (ret != NO_ERROR) {
        FLOGE("getSupportedRecordingFormat failed");
        mPreviewPixelFormat = HAL_PIXEL_FORMAT_YCbCr_420_SP;
        return;
    }

    mPreviewPixelFormat = getMatchFormat(vpuFormats, MAX_VPU_SUPPORT_FORMAT,
                          mAvailableFormats, MAX_SENSOR_FORMAT);
}

void DeviceAdapter::setPicturePixelFormat()
{
    int picFormats[MAX_PICTURE_SUPPORT_FORMAT];
    memset(picFormats, 0, sizeof(picFormats));
    int ret = mMetadaManager->getSupportedPictureFormat(picFormats,
                                MAX_PICTURE_SUPPORT_FORMAT);
    if (ret != NO_ERROR) {
        FLOGE("getSupportedPictureFormat failed");
        mPicturePixelFormat = HAL_PIXEL_FORMAT_YCbCr_420_SP;
        return;
    }
    mPicturePixelFormat = getMatchFormat(picFormats, MAX_PICTURE_SUPPORT_FORMAT,
                            mAvailableFormats, MAX_SENSOR_FORMAT);
}

static int GetCpuNum()
{
    int fd;
    int n;
    int cpuNum = 0;
    char data[1024];
    char *subStr = NULL;
    
    fd = open("/proc/cpuinfo", O_RDONLY);    
    if(fd < 0) {
        return 0;
    }

    n = read(fd, data, 1023);
    close(fd);

    if(n < 0 || n > 1023) {
        return 0;
    }

    data[n] = 0;

    subStr = data;
    while((subStr = strstr(subStr, "processor"))) {
        subStr++;
        cpuNum++;
        if(cpuNum > 100) //avoid dead cycle
            return 0;
    }

    return cpuNum;    
}
status_t DeviceAdapter::initialize(const CameraInfo& info)
{
    if (info.name == NULL) {
        FLOGE("invalid camera sensor name in initialize");
        return BAD_VALUE;
    }
    if (info.devPath == NULL) {
        FLOGE("invalid camera devpath in initialize");
        return BAD_VALUE;
    }

    if (info.devPath[0] != '\0') {
        mCameraHandle = open(info.devPath, O_RDWR);
    }
    if (mCameraHandle < 0) {
        memset((void*)info.devPath, 0, sizeof(info.devPath));
        GetDevPath(info.name, (char*)info.devPath, CAMAERA_FILENAME_LENGTH);
        if (info.devPath[0] != '\0') {
            mCameraHandle = open(info.devPath, O_RDWR);
            if (mCameraHandle < 0) {
                FLOGE("can not open camera devpath:%s", info.devPath);
                return BAD_VALUE;
            }
        }
        else {
            FLOGE("can not open camera devpath:%s", info.devPath);
            return BAD_VALUE;
        }
    }
    mVideoInfo = new VideoInfo();
    if (mVideoInfo == NULL) {
        close(mCameraHandle);
        FLOGE("new VideoInfo failed");
        return NO_MEMORY;
    }

    int ret = NO_ERROR;
    ret = ioctl(mCameraHandle, VIDIOC_QUERYCAP, &mVideoInfo->cap);
    if (ret < 0) {
        close(mCameraHandle);
        delete mVideoInfo;
        FLOGE("query v4l2 capability failed");
        return BAD_VALUE;
    }
    if ((mVideoInfo->cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0)
    {
        close(mCameraHandle);
        delete mVideoInfo;
        FLOGE("v4l2 capability does not support capture");
        return BAD_VALUE;
    }

    initSensorInfo(info);
    setPreviewPixelFormat();
    setPicturePixelFormat();
    // Initialize flags
    mPreviewing            = false;
    mVideoInfo->isStreamOn = false;
    mImageCapture          = false;

    mCpuNum = GetCpuNum();
    ALOGE("cpu num %d", mCpuNum);
        
    return NO_ERROR;
}

status_t DeviceAdapter::setDeviceConfig(int         width,
                                        int         height,
                                        PixelFormat format,
                                        int         fps)
{
    if (mCameraHandle <= 0) {
        FLOGE("setDeviceConfig: DeviceAdapter uninitialized");
        return BAD_VALUE;
    }
    if ((width == 0) || (height == 0)) {
        FLOGE("setDeviceConfig: invalid parameters");
        return BAD_VALUE;
    }

    status_t ret = NO_ERROR;
    int input    = 1;
    ret = ioctl(mCameraHandle, VIDIOC_S_INPUT, &input);
    if (ret < 0) {
        FLOGE("Open: VIDIOC_S_INPUT Failed: %s", strerror(errno));
        return ret;
    }

    int vformat;
    vformat = convertPixelFormatToV4L2Format(format);

    if ((width > 1920) || (height > 1080)) {
        fps = 15;
    }
    FLOGI("Width * Height %d x %d format 0x%x, fps: %d",
          width, height, vformat, fps);

    mVideoInfo->width       = width;
    mVideoInfo->height      = height;
    mVideoInfo->framesizeIn = (width * height << 1);
    mVideoInfo->formatIn    = vformat;

    struct v4l2_streamparm param;
    memset(&param, 0, sizeof(param));
    param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    param.parm.capture.timeperframe.numerator   = 1;
    param.parm.capture.timeperframe.denominator = fps;
    param.parm.capture.capturemode = getCaptureMode(width, height);
    ret = ioctl(mCameraHandle, VIDIOC_S_PARM, &param);
    if (ret < 0) {
        FLOGE("Open: VIDIOC_S_PARM Failed: %s", strerror(errno));
        return ret;
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type                 = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width        = width & 0xFFFFFFF8;
    fmt.fmt.pix.height       = height & 0xFFFFFFF8;
    fmt.fmt.pix.pixelformat  = vformat;
    fmt.fmt.pix.priv         = 0;
    fmt.fmt.pix.sizeimage    = 0;
    fmt.fmt.pix.bytesperline = 0;

    // Special stride alignment for YU12
    if (vformat == v4l2_fourcc('Y', 'U', '1', '2')){
        // Goolge define the the stride and c_stride for YUV420 format
        // y_size = stride * height
        // c_stride = ALIGN(stride/2, 16)
        // c_size = c_stride * height/2
        // size = y_size + c_size * 2
        // cr_offset = y_size
        // cb_offset = y_size + c_size
        // int stride = (width+15)/16*16;
        // int c_stride = (stride/2+16)/16*16;
        // y_size = stride * height
        // c_stride = ALIGN(stride/2, 16)
        // c_size = c_stride * height/2
        // size = y_size + c_size * 2
        // cr_offset = y_size
        // cb_offset = y_size + c_size

        // GPU and IPU take below stride calculation
        // GPU has the Y stride to be 32 alignment, and UV stride to be
        // 16 alignment.
        // IPU have the Y stride to be 2x of the UV stride alignment
        int stride = (width+31)/32*32;
        int c_stride = (stride/2+15)/16*16;
        fmt.fmt.pix.bytesperline = stride;
        fmt.fmt.pix.sizeimage    = stride*height+c_stride * height;
        FLOGI("Special handling for YV12 on Stride %d, size %d",
            fmt.fmt.pix.bytesperline,
            fmt.fmt.pix.sizeimage);
    }

    ret = ioctl(mCameraHandle, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        FLOGE("Open: VIDIOC_S_FMT Failed: %s", strerror(errno));
        return ret;
    }

    return ret;
}

int DeviceAdapter::getFrameSize()
{
    return mBufferSize;
}

int DeviceAdapter::getFrameCount()
{
    return mBufferCount;
}

status_t DeviceAdapter::registerCameraBuffers(CameraFrame *pBuffer,
                                             int        & num)
{
    status_t ret = NO_ERROR;

    if ((pBuffer == NULL) || (num <= 0)) {
        FLOGE("requestCameraBuffers invalid pBuffer");
        return BAD_VALUE;
    }

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof (req));
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_USERPTR;
    req.count  = num;

    ret = ioctl(mCameraHandle, VIDIOC_REQBUFS, &req);
    if (ret < 0) {
        FLOGE("VIDIOC_REQBUFS failed: %s", strerror(errno));
        return ret;
    }

    struct v4l2_buffer buf;
    for (int i = 0; i < num; i++) {
        CameraFrame *buffer = pBuffer + i;
        memset(&buf, 0, sizeof (buf));
        buf.index    = i;
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory   = V4L2_MEMORY_USERPTR;
        buf.m.offset = buffer->mPhyAddr;
        buf.length   = buffer->mSize;

        ret = ioctl(mCameraHandle, VIDIOC_QUERYBUF, &buf);
        if (ret < 0) {
            FLOGE("Unable to query buffer (%s)", strerror(errno));
            return ret;
        }

        // Associate each Camera buffer
        buffer->setObserver(this);
        mDeviceBufs[i] = buffer;
    }

    mBufferSize  = pBuffer->mSize;
    mBufferCount = num;

    return ret;
}

status_t DeviceAdapter::fillCameraFrame(CameraFrame *frame)
{
    status_t ret = NO_ERROR;

    if (!mVideoInfo->isStreamOn) {
        return NO_ERROR;
    }

    int i = frame->mIndex;
    if (i < 0) {
        return BAD_VALUE;
    }

    struct v4l2_buffer cfilledbuffer;
    memset(&cfilledbuffer, 0, sizeof (struct v4l2_buffer));
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cfilledbuffer.memory = V4L2_MEMORY_USERPTR;
    cfilledbuffer.index    = i;
    cfilledbuffer.m.offset = frame->mPhyAddr;

    ret = ioctl(mCameraHandle, VIDIOC_QBUF, &cfilledbuffer);
    if (ret < 0) {
        FLOGE("fillCameraFrame: VIDIOC_QBUF Failed");
        return BAD_VALUE;
    }
    android_atomic_inc(&mQueued);

    return ret;
}

status_t DeviceAdapter::startDeviceLocked()
{
    status_t ret = NO_ERROR;

    fAssert(mBufferProvider != NULL);

    int state;
    struct v4l2_buffer buf;
    for (int i = 0; i < mBufferCount; i++) {
        CameraFrame* frame = mDeviceBufs[i];
        state = frame->getState();
        if (state != CameraFrame::BUFS_FREE) {
            continue;
        }
        frame->setState(CameraFrame::BUFS_IN_DRIVER);

        memset(&buf, 0, sizeof (struct v4l2_buffer));
        buf.index    = i;
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory   = V4L2_MEMORY_USERPTR;
        buf.m.offset = frame->mPhyAddr;
        ret = ioctl(mCameraHandle, VIDIOC_QBUF, &buf);
        if (ret < 0) {
            FLOGE("VIDIOC_QBUF Failed");
            return BAD_VALUE;
        }

        android_atomic_inc(&mQueued);
    }

    enum v4l2_buf_type bufType;
    if (!mVideoInfo->isStreamOn) {
        bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ret = ioctl(mCameraHandle, VIDIOC_STREAMON, &bufType);
        if (ret < 0) {
            FLOGE("VIDIOC_STREAMON failed: %s", strerror(errno));
            return ret;
        }

        mVideoInfo->isStreamOn = true;
    }

    mDeviceThread = new DeviceThread(this);

    FLOGI("Created device thread");
    return ret;
}

status_t DeviceAdapter::stopDeviceLocked()
{
    status_t ret = NO_ERROR;
    enum v4l2_buf_type bufType;

    mDeviceThread->requestExitAndWait();
    mDeviceThread.clear();

    if (mVideoInfo->isStreamOn) {
        bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        ret = ioctl(mCameraHandle, VIDIOC_STREAMOFF, &bufType);
        if (ret < 0) {
            close(mCameraHandle);
            FLOGE("StopStreaming: Unable to stop capture: %s", strerror(errno));
            return ret;
        }

        mVideoInfo->isStreamOn = false;
    }

    mQueued   = 0;
    memset(mDeviceBufs, 0, sizeof(mDeviceBufs));

    return ret;
}

status_t DeviceAdapter::startPreview()
{
    status_t ret = NO_ERROR;

    if (mPreviewing) {
        FLOGE("DeviceAdapter: startPreview but preview running");
        return BAD_VALUE;
    }

    Mutex::Autolock lock(mBufsLock);
    mPreviewing = true;
    ret = startDeviceLocked();

    return ret;
}

status_t DeviceAdapter::stopPreview()
{
    int ret = NO_ERROR;

    if (!mPreviewing) {
        FLOGE("DeviceAdapter: stopPreview but preview not running");
        return NO_INIT;
    }

    Mutex::Autolock lock(mBufsLock);
    mPreviewing = false;
    ret         = stopDeviceLocked();

    return ret;
}

status_t DeviceAdapter::startImageCapture()
{
    status_t ret = NO_ERROR;

    if (mImageCapture) {
        FLOGE("DeviceAdapter: startPreview but preview running");
        return BAD_VALUE;
    }

    Mutex::Autolock lock(mBufsLock);
    mImageCapture = true;
    ret           = startDeviceLocked();

    return ret;
}

status_t DeviceAdapter::stopImageCapture()
{
    int ret = NO_ERROR;

    if (!mImageCapture) {
        FLOGE("DeviceAdapter: stopPreview but preview not running");
        return NO_INIT;
    }

    Mutex::Autolock lock(mBufsLock);
    mImageCapture = false;
    ret           = stopDeviceLocked();

    return ret;
}

CameraFrame * DeviceAdapter::acquireCameraFrame()
{
    int ret;

    struct v4l2_buffer cfilledbuffer;
    memset(&cfilledbuffer, 0, sizeof (cfilledbuffer));
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cfilledbuffer.memory = V4L2_MEMORY_USERPTR;

    /* DQ */
    ret = ioctl(mCameraHandle, VIDIOC_DQBUF, &cfilledbuffer);
    if (ret < 0) {
        FLOGE("GetFrame: VIDIOC_DQBUF Failed");
        return NULL;
    }
    android_atomic_dec(&mQueued);

    int index = cfilledbuffer.index;
    fAssert(index >= 0 && index < mBufferCount);
    mDeviceBufs[index]->mTimeStamp = systemTime();

    return mDeviceBufs[index];
}

// #define FSL_CAMERAHAL_DUMP
static void bufferDump(CameraFrame *frame)
{
#ifdef FSL_CAMERAHAL_DUMP

    // for test code
    char value[100];
    memset(value, 0, sizeof(value));
    static int vflg = 0;
    property_get("rw.camera.test", value, "");
    if (strcmp(value, "1") == 0)
        vflg = 1;
    if (vflg) {
        FILE *pf = NULL;
        pf = fopen("/sdcard/camera_tst.data", "wb");
        if (pf == NULL) {
            FLOGI("open /sdcard/camera_tst.data failed");
        }
        else {
            FLOGI("-----write-----");
            fwrite(frame->mVirtAddr, frame->mSize, 1, pf);
            fclose(pf);
        }
        vflg = 0;
    }
#endif // ifdef FSL_CAMERAHAL_DUMP
}

int DeviceAdapter::deviceThread()
{
    CameraFrame *frame = NULL;

    if (mQueued <= 0) {
        FLOGI("no buffer in v4l2, continue");
        usleep(10000); //sleep 10ms
        return NO_ERROR;
    }
    if (!mPreviewing && !mImageCapture) {
        FLOGI("device stop, deviceThread exit");
        return BAD_VALUE;
    }

    frame = acquireCameraFrame();
    if (!frame) {
        FLOGE("device thread exit with frame = null, %d buffers still in v4l",
              mQueued);
        if (mErrorListener != NULL) {
            mErrorListener->handleError(CAMERA2_MSG_ERROR_DEVICE);
        }
        return BAD_VALUE;
    }

    if (mImageCapture) {
        sp<CameraEvent> cameraEvt = new CameraEvent();
        cameraEvt->mEventType = CameraEvent::EVENT_SHUTTER;
        dispatchEvent(cameraEvt);

        frame->mFrameType = CameraFrame::IMAGE_FRAME;
    }
    else {
        frame->mFrameType = CameraFrame::PREVIEW_FRAME;
    }

    dispatchCameraFrame(frame);
    if (mImageCapture || !mPreviewing) {
        FLOGI("device thread exit...");
        return ALREADY_EXISTS;
    }

    return NO_ERROR;
}

status_t DeviceAdapter::autoFocus()
{
    if (mAutoFocusThread != NULL) {
        mAutoFocusThread.clear();
    }

    mAutoFocusThread = new AutoFocusThread(this);
    if (mAutoFocusThread == NULL) {
        return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

status_t DeviceAdapter::cancelAutoFocus()
{
    return NO_ERROR;
}

int DeviceAdapter::autoFocusThread()
{
    sp<CameraEvent> cameraEvt = new CameraEvent();
    cameraEvt->mEventType = CameraEvent::EVENT_FOCUS;
    dispatchEvent(cameraEvt);

    // exit the thread.
    return UNKNOWN_ERROR;
}

void DeviceAdapter::handleFrameRelease(CameraFrame *buffer)
{
    if (mPreviewing) {
        fillCameraFrame(buffer);
    }
}

void DeviceAdapter::setErrorListener(CameraErrorListener *listener)
{
    mErrorListener = listener;
}

void DeviceAdapter::setCameraBufferProvide(CameraBufferProvider *bufferProvider)
{
    if (bufferProvider != NULL) {
        bufferProvider->addBufferListener(this);
    }
    mBufferProvider = bufferProvider;
}

void DeviceAdapter::onBufferCreat(CameraFrame *pBuffer,
                                  int          num)
{
    registerCameraBuffers(pBuffer, num);
}

void DeviceAdapter::onBufferDestroy()
{
    memset(mDeviceBufs, 0, sizeof(mDeviceBufs));
}

