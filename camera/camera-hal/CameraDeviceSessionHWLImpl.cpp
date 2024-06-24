/*
 *  Copyright 2020-2024 NXP.
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
// #define LOG_NDEBUG 0
#define LOG_TAG "CameraDeviceSessionHwlImpl"

#include "CameraDeviceSessionHWLImpl.h"

#include <binder/MemoryHeapBase.h>
#include <hardware/camera3.h>
#include <hardware/gralloc.h>
#include <inttypes.h>
#include <log/log.h>
#include <sync/sync.h>
#include <ui/GraphicBufferAllocator.h>
#include <utils/Trace.h>

#include "CameraMetadata.h"
#include "CameraUtils.h"
#include "ImageProcess.h"

// using namespace fsl;
using namespace cameraconfigparser;
namespace android {

static uint32_t importCount;
static uint32_t freeCount;
// ImportFence and closeFence are refed from
// hardware/interfaces/camera/common/1.0/default/HandleImporter.cpp. If use functions from
// HandleImporter.cpp, will lead to add a series of libs.
static int importFence(const native_handle_t *handle, bool debug) {
    if (handle == nullptr || handle->numFds == 0)
        return -1;

    if (handle->numFds != 1) {
        ALOGE("invalid fence handle with %d file descriptors", handle->numFds);
        return -1;
    }

    int fd = dup(handle->data[0]);
    if (fd < 0)
        ALOGE("failed to dup fence fd %d, %s", handle->data[0], strerror(errno));

    importCount++;
    if (debug)
        ALOGI("importFence, fd %d, importCount %u, freeCount %u", fd, importCount, freeCount);

    return fd;
}

static void closeFence(int fd, bool debug) {
    if (fd >= 0) {
        freeCount++;
        if (debug)
            ALOGI("freeFence, fd %d, importCount %u, freeCount %u", fd, importCount, freeCount);

        close(fd);
    }
}

static void ItvlStat(uint64_t &preTime, char *name) {
    uint64_t curTime = systemTime();

    if (preTime > 0)
        ALOGI("%s itvl is %ld ms", name, (curTime - preTime) / 1000000);

    preTime = curTime;

    return;
}

std::unique_ptr<CameraDeviceSessionHwlImpl> CameraDeviceSessionHwlImpl::Create(
        uint32_t camera_id, std::unique_ptr<HalCameraMetadata> pMeta, CameraDeviceHwlImpl *pDev,
        PhysicalMetaMapPtr physical_devices) {
    if (pMeta.get() == nullptr) {
        return nullptr;
    }

    auto session = std::unique_ptr<CameraDeviceSessionHwlImpl>(
            new CameraDeviceSessionHwlImpl(std::move(physical_devices)));
    if (session == nullptr) {
        ALOGE("%s: Creating CameraDeviceSessionHwlImpl failed", __func__);
        return nullptr;
    }

    status_t res = session->Initialize(camera_id, std::move(pMeta), pDev);
    if (res != OK) {
        ALOGE("%s: session->Initialize  failed: %s(%d)", __func__, strerror(-res), res);
        return nullptr;
    }

    return session;
}

status_t CameraDeviceSessionHwlImpl::Initialize(uint32_t camera_id,
                                                std::unique_ptr<HalCameraMetadata> pMeta,
                                                CameraDeviceHwlImpl *pDev) {
    int ret;
    camera_id_ = camera_id;

    static_metadata_ = std::move(pMeta);

    if (pDev == NULL)
        return BAD_VALUE;

    camera_ = pDev->GetCamera();
    if (camera_ == nullptr) {
        ALOGE("%s,  camera_null !!!", __func__);
        return BAD_VALUE;
    }

    m_meta = pDev->m_meta->Clone();

    ALOGI("Initialize, meta %p, entry count %zu", static_metadata_.get(),
          static_metadata_->GetEntryCount());

    mDevPath = pDev->mDevPath;

    CameraSensorMetadata *cam_metadata = &(pDev->mSensorData);

    if ((physical_meta_map_.get() != nullptr) && (!physical_meta_map_->empty())) {
        is_logical_device_ = true;
        // If possible map the available focal lengths to individual physical devices
        camera_metadata_ro_entry_t logical_entry, physical_entry;
        ret = static_metadata_->Get(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS, &logical_entry);
        if ((ret == OK) && (logical_entry.count > 0)) {
            for (size_t i = 0; i < logical_entry.count; i++) {
                for (const auto &it : *physical_meta_map_) {
                    ret = it.second->Get(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
                                         &physical_entry);
                    if ((ret == OK) && (physical_entry.count > 0)) {
                        if (logical_entry.data.f[i] == physical_entry.data.f[0]) {
                            physical_focal_length_map_[physical_entry.data.f[0]] = it.first;
                            ALOGI("%s: current_focal_length_ camera id: %d\n", __FUNCTION__,
                                  it.first);
                            break;
                        }
                    }
                }
            }
        }
        current_focal_length_ = logical_entry.data.f[0];
        ALOGI("%s: current_focal_length_ set: %5.2f\n", __FUNCTION__, logical_entry.data.f[0]);
    }

    // create jpeg builder
    mJpegBuilder = new JpegBuilder();

    mInQueRequestIdx = 0;
    mDeQueRequestIdx = 0;

    // Device may be destroyed after create session, need copy some members from device.
    memcpy(mJpegHw, pDev->mJpegHw, JPEG_HW_NAME_LEN);
    mUseCpuEncoder = pDev->mUseCpuEncoder;
    mSensorData = pDev->mSensorData;

    mPreviewResolutionCount = pDev->mPreviewResolutionCount;
    memcpy(mPreviewResolutions, pDev->mPreviewResolutions, MAX_RESOLUTION_SIZE * sizeof(int));
    mPictureResolutionCount = pDev->mPictureResolutionCount;
    memcpy(mPictureResolutions, pDev->mPictureResolutions, MAX_RESOLUTION_SIZE * sizeof(int));

    mMaxWidth = pDev->mMaxWidth;
    mMaxHeight = pDev->mMaxHeight;

    camera_->acquire();
    camera_->requestCompleted.connect(this, &CameraDeviceSessionHwlImpl::requestComplete);

    return OK;
}

CameraDeviceSessionHwlImpl::CameraDeviceSessionHwlImpl(PhysicalMetaMapPtr physical_devices) {
    ALOGI("%s: this %p", __func__, this);

    memset(&m3aState, 0, sizeof(m3aState));

    m_meta = NULL;
    mSettings = NULL;
    mDebug = false;
    mPreCapAndFeedTime = 0;
    mPreSubmitRequestTime = 0;
    camera_ = nullptr;

    physical_meta_map_ = std::move(physical_devices);
}

CameraDeviceSessionHwlImpl::~CameraDeviceSessionHwlImpl() {
    ALOGI("%s: this %p, camera_ %p, %p", __func__, this, camera_.get());

    if (mJpegBuilder != NULL)
        mJpegBuilder.clear();

    if (mSettings != NULL)
        mSettings.reset();

    if (m_meta) {
        delete m_meta;
        m_meta = NULL;
    }

    if (camera_) {
        camera_->requestCompleted.disconnect();
        camera_->release();
    }
}

PipelineInfo *CameraDeviceSessionHwlImpl::GetPipelineInfo(uint32_t id) {
    auto it = map_pipeline_info.find(id);
    if (it != map_pipeline_info.end())
        return it->second;

    return NULL;
}

#define WAIT_TIME_OUT 100000000LL // unit ns, wait 100ms

void CameraDeviceSessionHwlImpl::DumpRequest() {
    ALOGI("%s: map_frame_request size %zu", __func__, map_frame_request.size());

    for (auto it = map_frame_request.begin(); it != map_frame_request.end(); it++) {
        uint32_t frame = it->first;
        ALOGI("%s: frame %u", __func__, frame);
    }
}

void CameraDeviceSessionHwlImpl::ReleaseFrameRequest(FrameRequest &frameRequest) {
    HwlPipelineRequest &hwReq = frameRequest.hwlReq;

    // clear hwReq
    hwReq.settings.reset();
    hwReq.output_buffers.clear();
    hwReq.input_buffers.clear();
    hwReq.input_buffer_metadata.clear();

    // close fence
    int size = frameRequest.outBufferFences.size();
    for (int i = 0; i < size; i++) {
        int acquire_fence_fd = frameRequest.outBufferFences[i].acquire_fence_fd;
        if (acquire_fence_fd > -1)
            closeFence(acquire_fence_fd, mDebug);

        frameRequest.outBufferFences[i].acquire_fence_fd = -1;
    }
    frameRequest.outBufferFences.clear();

    return;
}

Stream *CameraDeviceSessionHwlImpl::GetStreamFromStreamBuffer(StreamBuffer *buf) {
    if (buf == NULL)
        return NULL;

    ALOGV("%s: buf->stream_id %d", __func__, buf->stream_id);

    for (auto it = map_pipeline_info.begin(); it != map_pipeline_info.end(); it++) {
        PipelineInfo *pInfo = it->second;
        if (pInfo == NULL)
            return NULL;

        std::vector<Stream> *streams = pInfo->streams;
        int size = (int)streams->size();
        for (int i = 0; i < size; i++) {
            ALOGV("pInfo %p, streams[%d].id %d", pInfo, i, streams->at(i).id);
            if (streams->at(i).id == buf->stream_id)
                return &(streams->at(i));
        }
    }

    return NULL;
}

static void DumpStream(void *data, uint32_t size, int32_t id) {
    int fd = -1;

    if ((data == NULL) || (size == 0))
        return;

    ALOGI("%s: data size %d, stream id %d", __func__, size, id);

    char file[32];
    snprintf(file, 32, "/data/stream-%d.data", id);
    file[31] = 0;

    fd = open(file, O_CREAT | O_APPEND | O_WRONLY, S_IRWXU | S_IRWXG);

    if (fd < 0) {
        ALOGW("%s: file open error %s, file: %s, fd %d, ", __func__, strerror(errno), file, fd);
        return;
    }

    write(fd, data, size);

    close(fd);

    return;
}

int32_t CameraDeviceSessionHwlImpl::GetStreamIdFromLibcameraStream(
        const libcamera::Stream *libCameraStream) {
    for (auto &it : mLibCameraStreamMap) {
        if (it.second == libCameraStream)
            return it.first;
    }

    return -1;
}

void CameraDeviceSessionHwlImpl::DumpStreamWrapper(libcamera::Request *request) {
    if (request == NULL)
        return;

    char value[PROPERTY_VALUE_MAX];
    property_get("vendor.rw.camera.test", value, "");
    if ((strcmp(value, "") == 0) || (strcmp(value, "debug") == 0))
        return;

    int32_t streamIdBitVal = atoi(value);

    libcamera::Request::BufferMap bufMap = request->buffers();
    for (auto &t : bufMap) {
        const libcamera::Stream *libCameraStream = t.first;
        int32_t stream_id = GetStreamIdFromLibcameraStream(libCameraStream);
        if (stream_id < 0)
            continue;

        if ((streamIdBitVal & (1 << stream_id)) == 0)
            continue;

        libcamera::FrameBuffer *frameBuffer = t.second;
        const std::vector<libcamera::FrameBuffer::Plane> &planes = frameBuffer->planes();
        uint64_t addr = 0;
        int fd = planes[0].fd.get();
        uint32_t size = 0;
        uint32_t offset = planes[0].offset;
        void *virt = NULL;
        size_t plan_num = planes.size();

        for (int i = 0; i < plan_num; i++) {
            size += planes[i].length;
        }

        virt = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
        if (virt == NULL)
            return;

        DumpStream((uint8_t *)virt, size, stream_id);
        if (virt)
            munmap(virt, size);
    }

    return;
}

int32_t CameraDeviceSessionHwlImpl::processJpegBuffer(ImxStreamBuffer *srcBuf,
                                                      ImxStreamBuffer *dstBuf,
                                                      CameraMetadata *meta) {
    int32_t ret = 0;
    int32_t encodeQuality = 100, thumbQuality = 100;
    int32_t thumbWidth = 0, thumbHeight = 0;
    JpegParams *mainJpeg = NULL, *thumbJpeg = NULL;
    void *rawBuf = NULL, *thumbBuf = NULL;
    uint8_t *pDst = NULL;
    struct camera3_jpeg_blob *jpegBlob = NULL;
    uint32_t bufSize = 0;
    int maxJpegSize = mSensorData.maxjpegsize;
    ImxStreamBuffer resizeBuf;
    memset(&resizeBuf, 0, sizeof(resizeBuf));

    if ((srcBuf == NULL) || (dstBuf == NULL) || (meta == NULL)) {
        ALOGE("%s srcBuf %p, dstBuf %p, meta %p", __func__, srcBuf, dstBuf, meta);
        return BAD_VALUE;
    }

    ImxStream *srcStream = srcBuf->mStream;
    ImxStream *capture = dstBuf->mStream;

    if (capture == NULL || srcStream == NULL) {
        ALOGE("%s invalid param, capture %p, srcStream %p", __func__, capture, srcStream);
        return BAD_VALUE;
    }

    ret = meta->getJpegQuality(encodeQuality);
    if (ret != NO_ERROR) {
        ALOGE("%s getJpegQuality failed", __func__);
        return BAD_VALUE;
    }

    if ((encodeQuality < 0) || (encodeQuality > 100)) {
        encodeQuality = 100;
    }

    ret = meta->getJpegThumbQuality(thumbQuality);
    if (ret != NO_ERROR) {
        ALOGE("%s getJpegThumbQuality failed", __func__);
        return BAD_VALUE;
    }

    if ((thumbQuality < 0) || (thumbQuality > 100)) {
        thumbQuality = 100;
    }

    int captureSize = 0;
    int alignedw, alignedh, c_stride;
    switch (srcStream->format()) {
        case HAL_PIXEL_FORMAT_YCbCr_420_P:
            alignedw = ALIGN_PIXEL_32(capture->mWidth);
            alignedh = ALIGN_PIXEL_4(capture->mHeight);
            c_stride = (alignedw / 2 + 15) / 16 * 16;
            captureSize = alignedw * alignedh + c_stride * alignedh;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        case HAL_PIXEL_FORMAT_YCbCr_420_888:
            alignedw = ALIGN_PIXEL_16(capture->mWidth);
            alignedh = ALIGN_PIXEL_16(capture->mHeight);
            captureSize = alignedw * alignedh * 3 / 2;
            break;

        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            alignedw = ALIGN_PIXEL_16(capture->mWidth);
            alignedh = ALIGN_PIXEL_16(capture->mHeight);
            captureSize = alignedw * alignedh * 2;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
            alignedw = ALIGN_PIXEL_16(capture->mWidth);
            alignedh = ALIGN_PIXEL_16(capture->mHeight);
            captureSize = alignedw * alignedh * 2;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_444_888:
            alignedw = ALIGN_PIXEL_16(capture->mWidth);
            alignedh = ALIGN_PIXEL_16(capture->mHeight);
            captureSize = alignedw * alignedh * 3;
            break;

        default:
            ALOGE("Error: %s format 0x%x not supported", __func__, srcStream->format());
    }

    sp<MemoryHeapBase> rawFrame(new MemoryHeapBase(captureSize, 0, "rawFrame"));
    rawBuf = rawFrame->getBase();
    if (rawBuf == MAP_FAILED) {
        ALOGE("%s new MemoryHeapBase failed", __func__);
        return BAD_VALUE;
    }

    sp<MemoryHeapBase> thumbFrame(new MemoryHeapBase(captureSize, 0, "thumbFrame"));
    thumbBuf = thumbFrame->getBase();
    if (thumbBuf == MAP_FAILED) {
        ALOGE("%s new MemoryHeapBase failed", __func__);
        return BAD_VALUE;
    }

    // Handle zoom in
    if (srcStream->mZoomRatio > 1.0) {
        resizeBuf.mFormatSize = srcBuf->mFormatSize;
        ret = AllocPhyBuffer(srcBuf->mWidth, srcBuf->mHeight, srcBuf->mFormat, resizeBuf);
        if (ret) {
            ALOGE("%s:%d AllocPhyBuffer failed", __func__, __LINE__);
            return BAD_VALUE;
        }

        resizeBuf.mStream = srcBuf->mStream;
        handleFrame(resizeBuf, *srcBuf, mCamBlitCscType);

        SwitchImxBuf(*srcBuf, resizeBuf);
    }

    mainJpeg = new JpegParams((uint8_t *)srcBuf->mVirtAddr, (uint8_t *)(uintptr_t)srcBuf->mPhyAddr,
                              srcBuf->mSize, srcBuf->mFd, srcBuf->buffer, (uint8_t *)rawBuf,
                              captureSize, encodeQuality, srcStream->mWidth, srcStream->mHeight,
                              capture->mWidth, capture->mHeight, srcStream->format());

    ret = meta->getJpegThumbSize(thumbWidth, thumbHeight);
    if (ret != NO_ERROR) {
        ALOGE("%s getJpegThumbSize failed", __func__);
    }

    if ((thumbWidth > 0) && (thumbHeight > 0)) {
        int thumbSize = captureSize;
        thumbJpeg =
                new JpegParams((uint8_t *)srcBuf->mVirtAddr, (uint8_t *)(uintptr_t)srcBuf->mPhyAddr,
                               srcBuf->mSize, srcBuf->mFd, srcBuf->buffer, (uint8_t *)thumbBuf,
                               thumbSize, thumbQuality, srcStream->mWidth, srcStream->mHeight,
                               thumbWidth, thumbHeight, srcStream->format());
    }

    ret = mJpegBuilder->encodeImage(mainJpeg, thumbJpeg, mJpegHw, (*meta));
    if (ret != NO_ERROR) {
        ALOGE("%s encodeImage failed", __func__);
        goto err_out;
    }

    ret = mJpegBuilder->buildImage(dstBuf, mJpegHw);
    if (ret != NO_ERROR) {
        ALOGE("%s buildImage failed", __func__);
        goto err_out;
    }

    // write jpeg size
    pDst = (uint8_t *)dstBuf->mVirtAddr;
    bufSize = (maxJpegSize <= (int)dstBuf->mSize) ? maxJpegSize : dstBuf->mSize;

    jpegBlob = (struct camera3_jpeg_blob *)(pDst + bufSize - sizeof(struct camera3_jpeg_blob));
    jpegBlob->jpeg_blob_id = CAMERA3_JPEG_BLOB_ID;
    jpegBlob->jpeg_size = mJpegBuilder->getImageSize();

    ALOGI("%s, dstbuf size %d, %d, jpeg_size %d, max jpeg size %d", __func__, (int)dstBuf->mSize,
          captureSize, jpegBlob->jpeg_size, maxJpegSize);

err_out:
    if (mainJpeg != NULL)
        delete mainJpeg;

    if (thumbJpeg != NULL)
        delete thumbJpeg;

    if (resizeBuf.mPhyAddr > 0) {
        SwitchImxBuf(*srcBuf, resizeBuf);
        FreePhyBuffer(resizeBuf.buffer);
    }

    return ret;
}

status_t CameraDeviceSessionHwlImpl::HandleMetaLocked(
        std::unique_ptr<HalCameraMetadata> &resultMeta, uint64_t timestamp) {
    status_t ret;
    camera_metadata_ro_entry entry;

    ret = resultMeta->Get(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, &entry);
    if (ret != NAME_NOT_FOUND) {
        m3aState.aeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
        ALOGV("ae precature trigger");
    } else {
        m3aState.aeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
    }

    resultMeta->Set(ANDROID_CONTROL_AE_STATE, &m3aState.aeState, 1);

    ret = resultMeta->Get(ANDROID_CONTROL_AE_PRECAPTURE_ID, &entry);
    if (ret != NAME_NOT_FOUND) {
        m3aState.aeTriggerId = entry.data.i32[0];
    }

    resultMeta->Set(ANDROID_CONTROL_AE_PRECAPTURE_ID, &m3aState.aeTriggerId, 1);

    ret = resultMeta->Get(ANDROID_CONTROL_AF_TRIGGER_ID, &entry);
    if (ret != NAME_NOT_FOUND) {
        m3aState.afTriggerId = entry.data.i32[0];
    }

    resultMeta->Set(ANDROID_CONTROL_AF_TRIGGER_ID, &m3aState.afTriggerId, 1);

    resultMeta->Set(ANDROID_SENSOR_TIMESTAMP, (int64_t *)&timestamp, 1);

    // auto focus control.
    m3aState.afState = ANDROID_CONTROL_AF_STATE_INACTIVE;
    resultMeta->Set(ANDROID_CONTROL_AF_STATE, &m3aState.afState, 1);

    // auto white balance control.
    m3aState.awbState = ANDROID_CONTROL_AWB_STATE_CONVERGED;
    resultMeta->Set(ANDROID_CONTROL_AWB_STATE, &m3aState.awbState, 1);

    return OK;
}

static libcamera::PixelFormat HalFromat2PixelFormat(int halFmt) {
    libcamera::PixelFormat pixelFmt = libcamera::formats::YUYV;

    switch (halFmt) {
        case HAL_PIXEL_FORMAT_YCBCR_420_888:
        case HAL_PIXEL_FORMAT_YV12:
            pixelFmt = libcamera::formats::NV12;
            break;
        case HAL_PIXEL_FORMAT_YCBCR_422_I:
        case HAL_PIXEL_FORMAT_BLOB:
            pixelFmt = libcamera::formats::YUYV;
            break;
        default:
            ALOGW("%s: unsupported HalFromat 0x%x", __func__, halFmt);
            break;
    }

    return pixelFmt;
}

status_t CameraDeviceSessionHwlImpl::ConfigurePipeline(
        uint32_t physical_camera_id, HwlPipelineCallback hwl_pipeline_callback,
        const StreamConfiguration &request_config, const StreamConfiguration & /*overall_config*/,
        uint32_t *pipeline_id) {
    Mutex::Autolock _l(mLock);
    if (pipeline_id == nullptr) {
        ALOGE("%s pipeline_id is nullptr", __func__);
        return BAD_VALUE;
    }

    if (pipelines_built_) {
        ALOGE("%s Cannot configure pipelines after calling BuildPipelines()", __func__);
        return ALREADY_EXISTS;
    }

    bool bSupport =
            CameraDeviceHwlImpl::StreamCombJudge(request_config, mPreviewResolutions,
                                                 mPreviewResolutionCount, mPictureResolutions,
                                                 mPictureResolutionCount);

    if (bSupport == false) {
        ALOGI("%s: IsStreamCombinationSupported return false", __func__);
        return BAD_VALUE;
    }

    if ((physical_camera_id != camera_id_) && (physical_meta_map_.get() != nullptr)) {
        if (physical_meta_map_->find(physical_camera_id) == physical_meta_map_->end()) {
            ALOGE("%s: Camera: %d doesn't include physical device with id: %u", __FUNCTION__,
                  camera_id_, physical_camera_id);
            return BAD_VALUE;
        }
    }

    *pipeline_id = pipeline_id_;

    PipelineInfo *pipeline_info = (PipelineInfo *)calloc(1, sizeof(PipelineInfo));
    if (pipeline_info == NULL) {
        ALOGE("%s malloc pipeline_info failed", __func__);
        return BAD_VALUE;
    }

    pipeline_info->pipeline_id = pipeline_id_;
    pipeline_info->pipeline_callback = std::move(hwl_pipeline_callback);

    int stream_num = request_config.streams.size();
    if (stream_num == 0) {
        free(pipeline_info);
        ALOGE("%s stream num 0", __func__);
        return BAD_VALUE;
    }

    pipeline_info->streams = new std::vector<Stream>();
    pipeline_info->hal_streams = new std::vector<HalStream>();

    if ((pipeline_info->streams == NULL) || (pipeline_info->hal_streams == NULL)) {
        ALOGE("%s: no memory, pipeline_info->streams %p, pipeline_info->hal_streams %p", __func__,
              pipeline_info->streams, pipeline_info->hal_streams);
        return BAD_VALUE;
    }

    pipeline_info->streams->assign(request_config.streams.begin(), request_config.streams.end());

    previewIdx = -1;
    stillcapIdx = -1;
    recordIdx = -1;
    callbackIdx = -1;
    cameraRWIdx = -1;

    std::unique_ptr<libcamera::CameraConfiguration> camCfg = camera_->generateConfiguration();
    if (!camCfg) {
        ALOGE("%s: Failed to generate camera camCfguration", __func__);
        return BAD_VALUE;
    }
    ALOGI("%s: generate camera camCfguration, size %zu", __func__, camCfg->size());

    for (int i = 0; i < stream_num; i++) {
        Stream stream = request_config.streams[i];
        ALOGI("%s, stream %d: id %d, type %d, res %dx%d, format 0x%x, usage 0x%llx, space 0x%x, "
              "rot %d, is_phy %d, phy_id %d, size %d",
              __func__, i, stream.id, stream.stream_type, stream.width, stream.height,
              stream.format, (unsigned long long)stream.usage, stream.data_space, stream.rotation,
              stream.is_physical_camera_stream, stream.physical_camera_id, stream.buffer_size);

        uint32_t camera_id = camera_id_;
        camera_ids.push_back(camera_id);

        HalStream hal_stream;
        memset(&hal_stream, 0, sizeof(hal_stream));
        int usage = 0;
        char socType[128] = {0};
        property_get("ro.boot.soc_type", socType, "");
        ALOGI("%s: socType :%s \n", __FUNCTION__, socType);

        switch (stream.format) {
            case HAL_PIXEL_FORMAT_RAW16:
            case HAL_PIXEL_FORMAT_BLOB:
                ALOGI("%s create capture stream", __func__);
                hal_stream.override_format = stream.format;
                hal_stream.max_buffers = NUM_CAPTURE_BUFFER;
                // fix me, just for cpu jpeg encoder.
                stream.usage |= GRALLOC_USAGE_SW_WRITE_OFTEN;
                stillcapIdx = i;

                if (stream.format == HAL_PIXEL_FORMAT_BLOB) {
                    // fix me, mv GRALLOC_USAGE_SW_READ_OFTEN if hardware encode jpeg.
                    uint32_t bufferStride;
                    buffer_handle_t bufferHandle;
                    uint32_t format = HAL_PIXEL_FORMAT_YCBCR_422_I;
                    uint64_t usage = stream.usage | GRALLOC_USAGE_HW_CAMERA_WRITE |
                            GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_PRIVATE_3;
                    auto status =
                            GraphicBufferAllocator::get().allocate(stream.width, stream.height,
                                                                   format,
                                                                   /*layerCount=*/1, usage,
                                                                   &bufferHandle, &bufferStride,
                                                                   "NxpCamera");
                    if (status != ::android::OK) {
                        ALOGE("%s: failed to allocate buffer:%d x %d, format=%x, usage=%lx, ret=%d",
                              __func__, stream.width, stream.height, format, usage, status);
                        return BAD_VALUE;
                    }
                    ALOGI("%s: mStreamMidBufMap[%d] %p, this %p", __func__, stream.id, bufferHandle,
                          this);
                    mStreamMidBufMap[stream.id] = bufferHandle;
                }

                break;

            case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
                if (strcmp(mSensorData.v4l2_format, "nv12") == 0) {
                    if (strstr(socType, "imx93")) {
                        hal_stream.override_format = HAL_PIXEL_FORMAT_YV12;
                    } else {
                        hal_stream.override_format = HAL_PIXEL_FORMAT_YCBCR_420_888;
                    }
                } else
                    hal_stream.override_format = HAL_PIXEL_FORMAT_YCBCR_422_I;

                hal_stream.max_buffers = NUM_PREVIEW_BUFFER;
                usage = CAMERA_GRALLOC_USAGE;

                if (stream.usage & GRALLOC_USAGE_HW_VIDEO_ENCODER) {
                    ALOGI("%s create video recording stream", __func__);
                    hal_stream.override_format = HAL_PIXEL_FORMAT_YCBCR_420_888;
                    recordIdx = i;
                } else if (stream.usage &
                           (GRALLOC_USAGE_HW_CAMERA_WRITE | GRALLOC_USAGE_HW_CAMERA_READ)) {
                    ALOGI("%s create camera rw stream", __func__);
                    cameraRWIdx = i;
                } else {
                    ALOGI("%s create preview stream", __func__);
                    previewIdx = i;
                }
                break;

            default:
                ALOGI("%s create callback stream", __func__);
                hal_stream.override_format = stream.format;
                hal_stream.max_buffers = NUM_PREVIEW_BUFFER;
                usage = CAMERA_GRALLOC_USAGE;
                callbackIdx = i;
                break;
        }

        // May remove after refine mali gralloc
        stream.usage |= GRALLOC_USAGE_PRIVATE_3;

        if (stream.format != HAL_PIXEL_FORMAT_BLOB)
            stream.usage |= GRALLOC_USAGE_HW_CAMERA_WRITE;

        if (stream.usage & GRALLOC_USAGE_HW_TEXTURE)
            stream.usage |= GRALLOC_USAGE_SW_READ_OFTEN;

        hal_stream.producer_usage = stream.usage | usage;
        hal_stream.consumer_usage = 0;
        hal_stream.id = stream.id;
        hal_stream.override_data_space = stream.data_space;
        hal_stream.is_physical_camera_stream = stream.is_physical_camera_stream;
        hal_stream.physical_camera_id = stream.physical_camera_id;

        pipeline_info->hal_streams->push_back(std::move(hal_stream));

        int halFmtForLibcamera = hal_stream.override_format;
        if (halFmtForLibcamera == HAL_PIXEL_FORMAT_BLOB)
            halFmtForLibcamera = HAL_PIXEL_FORMAT_YCBCR_422_I;

        libcamera::StreamConfiguration cfg;
        cfg.bufferCount = 4;
        cfg.size.width = stream.width;
        cfg.size.height = stream.height;
        cfg.pixelFormat = HalFromat2PixelFormat(hal_stream.override_format);
        camCfg->addConfiguration(cfg);

        ALOGI("%s: after adjust, usage 0x%llx", __func__, stream.usage);
    }

    int ret = camera_->configure(camCfg.get());
    if (ret) {
        ALOGE("%s: Failed to configure camera %s", __func__, camera_->id().c_str());
        return ret;
    }

    std::set<libcamera::Stream *> libCameraStreamSet = camera_->streams();
    ALOGI("%s: libCameraStreamSet size %d, stream_num %d", __func__, libCameraStreamSet.size(),
          stream_num);
    if (libCameraStreamSet.size() < stream_num) {
        ALOGE("%s: beyond capbility, libCameraStreamSet size %d < stream_num %d", __func__,
              libCameraStreamSet.size(), stream_num);
        return BAD_VALUE;
    }

    int i = 0;
    // Fix me if libCameraStreamSet is not sequenced by config sequence.
    for (const auto &libcameraStream : libCameraStreamSet) {
        Stream stream = request_config.streams[i];
        mLibCameraStreamMap[stream.id] = libcameraStream;
        ALOGI("%s: set mLibCameraStreamMap i %d, id %d, libcamera::Stream %p, mLibCameraStreamMap size %d",
              __func__, i, stream.id, libcameraStream, mLibCameraStreamMap.size());
        i++;

        if (i >= stream_num)
            break;
    }

    ALOGI("%s: pipeline_id_ %d, info %p, map_pipeline_info %p, this %p", __func__, pipeline_id_,
          pipeline_info, &map_pipeline_info, this);
    map_pipeline_info[pipeline_id_] = pipeline_info;
    pipeline_id_++;

    return OK;
}

status_t CameraDeviceSessionHwlImpl::GetConfiguredHalStream(
        uint32_t pipeline_id, std::vector<HalStream> *hal_streams) const {
    // fix me, build error as
    // error: 'this' argument to member function 'lock' has type 'const android::Mutex', but
    // function is not marked const Mutex::Autolock _l(mLock);

    if (hal_streams == nullptr) {
        ALOGE("%s hal_streams is nullptr", __func__);
        return BAD_VALUE;
    }

    if (!pipelines_built_) {
        ALOGE("%s No pipeline was built.", __func__);
        return NO_INIT;
    }

    auto iter = map_pipeline_info.find(pipeline_id);
    if (iter == map_pipeline_info.end()) {
        ALOGE("%s: Unknown pipeline ID: %u", __func__, pipeline_id);
        return NAME_NOT_FOUND;
    }

    bool found = false;
    for (auto it = map_pipeline_info.begin(); it != map_pipeline_info.end(); it++) {
        if (pipeline_id != it->first)
            continue;

        found = true;
        std::vector<HalStream> *streams = it->second->hal_streams;
        hal_streams->reserve(streams->size());
        hal_streams->assign(streams->begin(), streams->end());
        break;
    }

    return found ? OK : NAME_NOT_FOUND;
}

status_t CameraDeviceSessionHwlImpl::BuildPipelines() {
    ALOGI("enter %s", __func__);

    Mutex::Autolock _l(mLock);

    if (pipelines_built_) {
        ALOGE("%s Pipelines have already been built!", __func__);
        return ALREADY_EXISTS;
    } else if (map_pipeline_info.size() == 0) {
        ALOGE("%s No pipelines have been configured yet!", __func__);
        return NO_INIT;
    }

    pipelines_built_ = true;

    return OK;
}

void CameraDeviceSessionHwlImpl::DestroyPipelines() {
    ALOGI("enter %s", __func__);

    if (!pipelines_built_) {
        // Not an error - nothing to destroy
        ALOGV("%s nothing to destroy", __func__);
        return;
    }

    if (state_ == CameraState::Running) {
        int ret = camera_->stop();
        if (ret) {
            ALOGW("%s: Failed to stop camera", __func__);
        }
        state_ = CameraState::Stopped;
    }

    // camera_->stop() (in command thead) will call d->pipe_->invokeMethod(&PipelineHandler::stop,
    // ConnectionTypeBlocking, this); PipelineHandler::stop() and requestComplete() is in same
    // thread(say v4l2 thread). To avoid dead lock, hold locks with same sequence: Lock for
    // ConnectionTypeBlocking, mLock.

    Mutex::Autolock _l(mLock);
    /* If still has on-fly requests from map_frame_request, wait to finish */
    while (mDeQueRequestIdx != mInQueRequestIdx) {
        ALOGW("%s: still has requests to process, wait %d us, DeQueIdx %lu, InQueIdx %lu", __func__,
              WAIT_ITVL_US, mDeQueRequestIdx, mInQueRequestIdx);
        mLock.unlock();
        usleep(WAIT_ITVL_US);
        mLock.lock();
    }

    ALOGI("%s: requests, mDeQueRequestIdx %lu, mInQueRequestIdx %lu", __func__, mDeQueRequestIdx,
          mInQueRequestIdx);

    /* clear mFrameBuffers */
    for (auto it = mFrameBuffers.begin(); it != mFrameBuffers.end(); it++) {
        ALOGW("%s: still has FrameBuffer %p", __func__, (*it).get());
        (*it).reset();
        mFrameBuffers.erase(it);
    }

    CleanRequestsLocked();

    /* clear  map_pipeline_info */
    for (auto it = map_pipeline_info.begin(); it != map_pipeline_info.end(); it++) {
        auto pInfo = it->second;
        if (pInfo == NULL) {
            ALOGW("%s, no pipeline info for id %u", __func__, it->first);
            continue;
        }

        if (pInfo->streams) {
            pInfo->streams->clear();
            delete pInfo->streams;
        }

        if (pInfo->hal_streams) {
            pInfo->hal_streams->clear();
            delete pInfo->hal_streams;
        }

        free(pInfo);
    }

    map_pipeline_info.clear();

    /* clear mStreamMidBufMap */
    for (auto &t : mStreamMidBufMap) {
        int32_t id = t.first;
        ALOGI("%s: free MidBuf %p for stream %d, this %p", __func__, t.second, id, this);
        GraphicBufferAllocator::get().free(t.second);
    }
    mStreamMidBufMap.clear();

    mLibCameraStreamMap.clear();

    pipelines_built_ = false;
}

#define MAX_PLAN 3
typedef struct tagPlanInfo {
    uint32_t size;
    uint32_t offset;
} PlanInfo;

typedef struct tagPlansInfo {
    uint32_t num;
    PlanInfo plans[MAX_PLAN];
} PlansInfo;

static uint32_t GetPlansInfo(const libcamera::StreamConfiguration &streamConfig,
                             PlansInfo &plansInfo) {
    uint32_t width = streamConfig.size.width;
    uint32_t height = streamConfig.size.height;

    switch (streamConfig.pixelFormat) {
        case libcamera::formats::NV12:
            plansInfo.num = 1;
            plansInfo.plans[0].offset = 0;
            plansInfo.plans[0].size = width * height * 3 / 2;
            break;
        case libcamera::formats::YUYV:
            plansInfo.num = 1;
            plansInfo.plans[0].offset = 0;
            plansInfo.plans[0].size = width * height * 2;
            break;
        default:
            ALOGE("%s: unsupported pixelFormat %s", __func__,
                  streamConfig.pixelFormat.toString().c_str());
            return BAD_VALUE;
    }

    return 0;
}

std::unique_ptr<libcamera::FrameBuffer> CameraDeviceSessionHwlImpl::CreateFrameBuffer(
        const buffer_handle_t hnd, const libcamera::StreamConfiguration &streamConfig) {
    int ret = 0;
    PlansInfo plansInfo;

    ret = GetPlansInfo(streamConfig, plansInfo);
    if (ret) {
        ALOGE("%s: GetPlansInfo failed", __func__);
        return nullptr;
    }

    uint32_t planNum = plansInfo.num;
    std::vector<libcamera::FrameBuffer::Plane> planes(planNum);
    for (size_t i = 0; i < planNum; ++i) {
        libcamera::SharedFD fd{hnd->data[i]};
        if (!fd.isValid()) {
            ALOGE("%s: No valid fd %d", __func__, hnd->data[i]);
            return nullptr;
        }

        planes[i].fd = fd;
        planes[i].offset = plansInfo.plans[i].offset;
        planes[i].length = plansInfo.plans[i].size;

        ALOGV("%s:, plan %d, fd %d, offset %d, length %d", __func__, i, fd.get(), planes[i].offset,
              planes[i].length);
    }

    return std::make_unique<libcamera::FrameBuffer>(std::move(planes));
}

Stream *CameraDeviceSessionHwlImpl::GetStreamById(int32_t stream_id, PipelineInfo *pInfo) {
    if (pInfo == NULL)
        return NULL;

    uint32_t stream_num = pInfo->streams->size();
    for (int i = 0; i < stream_num; i++) {
        if (pInfo->streams->at(i).id == stream_id)
            return &pInfo->streams->at(i);
    }

    return NULL;
}

status_t CameraDeviceSessionHwlImpl::SubmitRequests(uint32_t frame_number,
                                                    std::vector<HwlPipelineRequest> &requests) {
    if (state_ == CameraState::Stopped) {
        int ret = camera_->start();
        if (ret) {
            ALOGE("%s: Failed to start camera", __func__);
            return ret;
        }
        state_ = CameraState::Running;
    }

    int size = requests.size();
    std::vector<FrameRequest> *frame_request = new std::vector<FrameRequest>(size);
    std::list<std::unique_ptr<libcamera::FrameBuffer>> frameBuffers;

    for (int i = 0; i < size; i++) {
        uint32_t pipeline_id = requests[i].pipeline_id;

        if (mDebug)
            ALOGI("%s, frame_number %d, pipeline_id %d, outbuffer num %d", __func__, frame_number,
                  (int)pipeline_id, (int)requests[i].output_buffers.size());

        PipelineInfo *pInfo = GetPipelineInfo(pipeline_id);
        if (pInfo == NULL) {
            ALOGE("%s: Unexpected, pipeline %d is invalid", __func__, pipeline_id);
            return BAD_VALUE;
        }

        frame_request->at(i).idx = i;
        frame_request->at(i).num = size;
        frame_request->at(i).vector = (void *)frame_request;

        frame_request->at(i).frame_number = frame_number;
        frame_request->at(i).hwlReq.pipeline_id = pipeline_id;
        frame_request->at(i).hwlReq.settings = HalCameraMetadata::Clone(requests[i].settings.get());
        frame_request->at(i).hwlReq.output_buffers.reserve(requests[i].output_buffers.size());
        frame_request->at(i).hwlReq.output_buffers.assign(requests[i].output_buffers.begin(),
                                                          requests[i].output_buffers.end());

        frame_request->at(i).hwlReq.input_buffers.reserve(requests[i].input_buffers.size());
        frame_request->at(i).hwlReq.input_buffers.assign(requests[i].input_buffers.begin(),
                                                         requests[i].input_buffers.end());
        frame_request->at(i).hwlReq.input_buffer_metadata.reserve(
                requests[i].input_buffer_metadata.size());

        frame_request->at(i).camera_ids.reserve(camera_ids.size());
        frame_request->at(i).camera_ids.assign(camera_ids.begin(), camera_ids.end());

        frame_request->at(i).request =
                camera_->createRequest(reinterpret_cast<uint64_t>(&frame_request->at(i)));

        // Record fence fd, add buffer to request
        uint32_t outBufNum = requests[i].output_buffers.size();
        frame_request->at(i).outBufferFences.resize(outBufNum);
        for (int j = 0; j < (int)outBufNum; j++) {
            FenceFdInfo fenceInfo = {-1, -1};
            fenceInfo.acquire_fence_fd =
                    importFence(requests[i].output_buffers[j].acquire_fence, mDebug);
            frame_request->at(i).outBufferFences[j] = fenceInfo;

            int32_t stream_id = requests[i].output_buffers[j].stream_id;
            if (mDebug)
                ALOGI("%s, acquire_fence_fd %d, stream_id %d for buf %d", __func__,
                      fenceInfo.acquire_fence_fd, stream_id, j);

            auto iter = mLibCameraStreamMap.find(stream_id);
            if (iter == mLibCameraStreamMap.end()) {
                ALOGE("%s: no stream_id %d in mLibCameraStreamMap", __func__, stream_id);
                return BAD_VALUE;
            }
            libcamera::Stream *libCameraStream = mLibCameraStreamMap[stream_id];

            Stream *stream = GetStreamById(stream_id, pInfo);
            if (stream == NULL) {
                ALOGE("%s: unexpected stream NULL for id %d", __func__, stream_id);
                return BAD_VALUE;
            }

            buffer_handle_t hnd = NULL;
            if (stream->format != HAL_PIXEL_FORMAT_BLOB)
                hnd = requests[i].output_buffers[j].buffer;
            else {
                auto iter = mStreamMidBufMap.find(stream_id);
                if (iter == mStreamMidBufMap.end()) {
                    ALOGE("%s: no stream id %d in mStreamMidBufMap", __func__, stream_id);
                    return BAD_VALUE;
                }
                hnd = mStreamMidBufMap[stream_id];
            }

            std::unique_ptr<libcamera::FrameBuffer> frameBuffer =
                    CreateFrameBuffer(hnd, libCameraStream->configuration());
            if (frameBuffer == nullptr) {
                ALOGE("%s, CreateFrameBuffer faliled for frame %d, request %d, outbuffer %d",
                      __func__, frame_number, i, j);
                return BAD_VALUE;
            }

            libcamera::UniqueFD fd(fenceInfo.acquire_fence_fd);
            std::unique_ptr<libcamera::Fence> fence =
                    std::make_unique<libcamera::Fence>(std::move(fd));
            frame_request->at(i).request->addBuffer(libCameraStream, frameBuffer.get(),
                                                    std::move(fence));
            frameBuffers.push_back(std::move(frameBuffer));

            if (mDebug)
                ALOGI("%s: outbuf %d, frameBuffer %p, acquire_fence_fd %d, hnd %p", __func__, j,
                      frameBuffer.get(), fenceInfo.acquire_fence_fd, hnd);
        }

        int ret = camera_->queueRequest(frame_request->at(i).request.get());
        if (ret) {
            ALOGE("%s, camera_->queueRequest failed, ret %d", __func__, ret);
            return ret;
        }
    }

    Mutex::Autolock _l(mLock);
    map_frame_request[frame_number] = frame_request;
    mInQueRequestIdx++;

    mFrameBuffers.splice(mFrameBuffers.end(), frameBuffers);

    char value[PROPERTY_VALUE_MAX];
    property_get("vendor.rw.camera.test", value, "");
    mDebug = (strcmp(value, "debug") == 0) ? true : false;

    if (mDebug) {
        ALOGI("%s: mInQueRequestIdx %lu, mDeQueRequestIdx %lu", __func__, mInQueRequestIdx,
              mDeQueRequestIdx);
        ItvlStat(mPreSubmitRequestTime, (char *)"SubmitRequests");
    }

    return OK;
}

int CameraDeviceSessionHwlImpl::CleanRequestsLocked() {
    if (map_frame_request.empty())
        return 0;

    for (auto it = map_frame_request.begin(); it != map_frame_request.end(); it++) {
        uint32_t frame = it->first;
        std::vector<FrameRequest> *request = it->second;
        if (request == NULL) {
            ALOGW("%s: frame %d request is null", __func__, frame);
            continue;
        }

        uint32_t reqNum = request->size();
        ALOGV("%s, map_frame_request, frame %d, reqNum %d", __func__, frame, reqNum);

        for (uint32_t i = 0; i < reqNum; i++) ReleaseFrameRequest(request->at(i));

        request->clear();
        delete request;
    }

    map_frame_request.clear();

    return 0;
}

status_t CameraDeviceSessionHwlImpl::Flush() {
    // TODO need refine for multi camera??
    return OK;
}

uint32_t CameraDeviceSessionHwlImpl::GetCameraId() const {
    return camera_id_;
}

std::vector<uint32_t> CameraDeviceSessionHwlImpl::GetPhysicalCameraIds() const {
    if ((physical_meta_map_->empty())) {
        ALOGV("%s: GetPhysicalCameraIds is empty", __func__);
        return std::vector<uint32_t>{};
    }

    if ((physical_meta_map_.get() == nullptr)) {
        ALOGW("%s: GetPhysicalCameraIds is null", __func__);
        return std::vector<uint32_t>{};
    }
    std::vector<uint32_t> ret;
    ret.reserve(physical_meta_map_->size());
    for (const auto &it : *physical_meta_map_) {
        ret.push_back(it.first);
    }

    return ret;
}

status_t CameraDeviceSessionHwlImpl::GetCameraCharacteristics(
        std::unique_ptr<HalCameraMetadata> *characteristics) const {
    if (characteristics == nullptr) {
        return BAD_VALUE;
    }

    (*characteristics) = HalCameraMetadata::Clone(static_metadata_.get());
    if (*characteristics == nullptr) {
        ALOGE("%s metadata clone failed", __func__);
        return NO_MEMORY;
    }

    return OK;
}

status_t CameraDeviceSessionHwlImpl::GetPhysicalCameraCharacteristics(
        uint32_t physical_camera_id, std::unique_ptr<HalCameraMetadata> *characteristics) const {
    if (characteristics == nullptr) {
        return BAD_VALUE;
    }

    if (physical_meta_map_.get() == nullptr) {
        ALOGE("%s: Camera: %d doesn't have physical device support!", __FUNCTION__, camera_id_);
        return BAD_VALUE;
    }

    if (physical_meta_map_->find(physical_camera_id) == physical_meta_map_->end()) {
        ALOGE("%s: Camera: %d doesn't include physical device with id: %u", __FUNCTION__,
              camera_id_, physical_camera_id);
        return BAD_VALUE;
    }

    *characteristics = HalCameraMetadata::Clone((physical_meta_map_->at(physical_camera_id)).get());
    return OK;
}

status_t CameraDeviceSessionHwlImpl::ConstructDefaultRequestSettings(
        RequestTemplate type, std::unique_ptr<HalCameraMetadata> *default_settings) {
    Mutex::Autolock _l(mLock);

    return m_meta->getRequestSettings(type, default_settings);
}

uint64_t CameraDeviceSessionHwlImpl::GetTimestamp(libcamera::Request *request) {
    Mutex::Autolock _l(mLock);
    libcamera::Request::BufferMap bufMap = request->buffers();
    for (auto &t : bufMap) {
        libcamera::FrameBuffer *frameBuffer = t.second;
        if (frameBuffer)
            return frameBuffer->metadata().timestamp;
    }

    ALOGW("!!! %s: unexpected, no valid frameBuffer in bufMap, size %d", __func__, bufMap.size());

    return systemTime(SYSTEM_TIME_MONOTONIC);
}

void CameraDeviceSessionHwlImpl::requestComplete(libcamera::Request *request) {
    if (request == NULL) {
        ALOGE("%s: request NULL", __func__);
        return;
    }

    FrameRequest *frameRequest = reinterpret_cast<FrameRequest *>(request->cookie());
    if (frameRequest == NULL) {
        ALOGE("%s: frameRequest NULL", __func__);
        return;
    }

    HwlPipelineRequest *hwReq = &frameRequest->hwlReq;
    if (hwReq == NULL) {
        ALOGE("%s: hwReq NULL", __func__);
        return;
    }

    // save the latest meta
    if (hwReq->settings != NULL) {
        if (mSettings != NULL)
            mSettings.reset();

        mSettings = HalCameraMetadata::Clone(hwReq->settings.get());
    }

    uint32_t pipeline_id = hwReq->pipeline_id;
    PipelineInfo *pInfo = GetPipelineInfo(pipeline_id);
    if (pInfo == NULL) {
        ALOGE("%s: Unexpected, pipeline %d is invalid", __func__, pipeline_id);
        return;
    }

    uint32_t frame = frameRequest->frame_number;

    // notify shutter
    uint64_t readout_timestamp_ns = GetTimestamp(request);
    uint64_t timestamp_ns = readout_timestamp_ns - 16666666;
    if (pInfo->pipeline_callback.notify) {
        NotifyMessage msg{.type = MessageType::kShutter,
                          .message.shutter = {.frame_number = frame,
                                              .timestamp_ns = timestamp_ns,
                                              .readout_timestamp_ns = readout_timestamp_ns}};

        pInfo->pipeline_callback.notify(pipeline_id, msg);
    }

    // clone latest meta to result->result_metadata
    auto result = std::make_unique<HwlPipelineResult>();
    if (mSettings != NULL)
        result->result_metadata = HalCameraMetadata::Clone(mSettings.get());
    else
        result->result_metadata = HalCameraMetadata::Create(1, 10);

    CameraMetadata requestMeta(result->result_metadata.get());

    // construct result
    result->camera_id = camera_id_;
    result->pipeline_id = pipeline_id;
    result->frame_number = frame;
    result->partial_result = 1;

    result->output_buffers.assign(hwReq->output_buffers.begin(), hwReq->output_buffers.end());
    result->input_buffers.reserve(0);
    result->physical_camera_results.reserve(0);

    if (mDebug) {
        ALOGI("%s: frame %d, output_buffers %d, result->regsult_metadata %p, entry count %d, libcamera::Request buffers %d, sequence %u",
              __func__, frame, result->output_buffers.size(), result->result_metadata.get(),
              (int)result->result_metadata->GetEntryCount(), request->buffers().size(),
              request->sequence());
    }

    std::vector<StreamBuffer> &output_buffers = hwReq->output_buffers;
    for (int i = 0; i < hwReq->output_buffers.size(); i++) {
        StreamBuffer *streamBuffer = &output_buffers[i];
        Stream *stream = GetStreamFromStreamBuffer(streamBuffer);
        if (stream == NULL) {
            ALOGE("%s: unexptect!!! no stream found for outBuf %d", __func__, i);
            continue;
        }

        if (stream->format != HAL_PIXEL_FORMAT_BLOB)
            continue;

        int32_t stream_id = stream->id;
        mJpegBuilder->reset();
        mJpegBuilder->setMetadata(&requestMeta);

        ImxStreamBuffer *dstBuf =
                CreateImxStreamBufferFromBufferHandle(streamBuffer->buffer, stream);
        if (dstBuf == NULL) {
            ALOGE("%s: dstBuf NULL", __func__);
            continue;
        }

        auto iter = mStreamMidBufMap.find(stream_id);
        if (iter == mStreamMidBufMap.end()) {
            ReleaseImxStreamBuffer(dstBuf);
            ALOGE("%s: no stream id %d in mStreamMidBufMap", __func__, stream_id);
            continue;
        }

        buffer_handle_t hnd = mStreamMidBufMap[stream_id];
        uint32_t size = getSizeByForamtRes(HAL_PIXEL_FORMAT_YCbCr_422_I, stream->width,
                                           stream->height, false);
        ImxStreamBuffer *srcBuf = CreateImxStreamBufferFromBufferHandle(hnd, stream);
        if (srcBuf == NULL) {
            ReleaseImxStreamBuffer(dstBuf);
            ALOGE("%s: srcBuf NULL", __func__);
            continue;
        }

        processJpegBuffer(srcBuf, dstBuf, &requestMeta);

        ReleaseImxStreamBuffer(dstBuf);
        ReleaseImxStreamBuffer(srcBuf);
    }

    DumpStreamWrapper(request);
    HandleMetaLocked(result->result_metadata, timestamp_ns);

    // call back to process result
    if (pInfo->pipeline_callback.process_pipeline_result) {
        pInfo->pipeline_callback.process_pipeline_result(std::move(result));
    }

    if (mDebug)
        ItvlStat(mPreHandleImageTime, (char *)"requestComplete(), process_pipeline_result");

    Mutex::Autolock _l(mLock);
    libcamera::Request::BufferMap bufMap = request->buffers();
    for (auto &t : bufMap) {
        libcamera::FrameBuffer *frameBuffer = t.second;
        ALOGV("%s: bufMap frameBuffer %p", __func__, frameBuffer);
        for (auto it = mFrameBuffers.begin(); it != mFrameBuffers.end(); it++) {
            ALOGV("%s: mFrameBuffers frameBuffer %p", __func__, (*it).get());
            if (frameBuffer != (*it).get())
                continue;
            ALOGV("%s: release frameBuffer %p", __func__, frameBuffer);
            (*it).reset();
            mFrameBuffers.erase(it);
        }
    }

    ALOGV("%s: mFrameBuffers size %d", __func__, mFrameBuffers.size());

    // Till now, always 1 frame, 1 request. But consider GCH interface
    // SubmitRequests(uint32_t frame_number, std::vector<HwlPipelineRequest> &requests),
    // need wait last FrameRequest, then erase the item in map_frame_request.
    if (frameRequest->idx == frameRequest->num - 1) {
        map_frame_request.erase(frame);
        std::vector<FrameRequest> *request = (std::vector<FrameRequest> *)frameRequest->vector;
        if (request) {
            request->clear();
            delete request;
        }
    }

    mDeQueRequestIdx++;
    if (mDebug)
        ALOGI("%s: mInQueRequestIdx %lu, mDeQueRequestIdx %lu", __func__, mInQueRequestIdx,
              mDeQueRequestIdx);

    return;
}

} // namespace android
