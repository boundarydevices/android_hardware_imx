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
    mCamBlitCopyType = pDev->mCamBlitCopyType;
    mCamBlitCscType = pDev->mCamBlitCscType;
    memcpy(mJpegHw, pDev->mJpegHw, JPEG_HW_NAME_LEN);
    mUseCpuEncoder = pDev->mUseCpuEncoder;
    mSensorData = pDev->mSensorData;
    if (strcmp(mSensorData.v4l2_format, "nv12") == 0)
        m_libcamera_stream_format = HAL_PIXEL_FORMAT_YCBCR_420_888;
    else
        m_libcamera_stream_format = HAL_PIXEL_FORMAT_YCBCR_422_I;

    if (strstr(mSensorData.camera_name, "os08a20")) {
        m_libcamera_stream_width = OS08A20_SENSOR_WIDTH;
        m_libcamera_stream_height = OS08A20_SENSOR_HEIGHT;
    } else {
        m_libcamera_stream_width = AP1302_SENSOR_WIDTH;
        m_libcamera_stream_height = AP1302_SENSOR_HEIGHT;
    }

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
    m_IspWrapper = std::make_unique<ISPWrapper>();
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
    ALOGI("%s: map_frame_request size %zu, addr %p", __func__, map_frame_request.size(),
          &map_frame_request);

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

int32_t CameraDeviceSessionHwlImpl::processFrameBuffer(ImxStreamBuffer *srcBuf,
                                                       ImxStreamBuffer *dstBuf,
                                                       CameraMetadata *meta) {
    if ((srcBuf == NULL) || (dstBuf == NULL) || (meta == NULL)) {
        ALOGE("%s srcBuf %p, dstBuf %p, meta %p", __func__, srcBuf, dstBuf, meta);
        return BAD_VALUE;
    }

    ImxEngine engine;

    ImxStream *srcStream = srcBuf->mStream;
    ImxStream *dstStream = dstBuf->mStream;

    if (srcStream->mWidth == dstStream->mWidth && srcStream->mHeight == dstStream->mHeight &&
        srcStream->format() == dstStream->format() && srcStream->mZoomRatio <= 1.0)
        engine = mCamBlitCopyType;
    else
        engine = mCamBlitCscType;

    return handleFrame(*dstBuf, *srcBuf, engine);
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
              __func__, i, stream.id, (int)stream.stream_type, stream.width, stream.height,
              stream.format, (unsigned long long)stream.usage, stream.data_space, (int)stream.rotation,
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
                break;

            case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
                if (strcmp(mSensorData.v4l2_format, "nv12") == 0) {
                    ALOGI("HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, use nv12");
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

        // libimageprocess is used to convert v4l2 buffer to output buffer. It requests valid virt
        // addr. Since MALI GPU lock requests CPU usages, so add GRALLOC_USAGE_SW_WRITE_OFTEN.
        stream.usage |= GRALLOC_USAGE_SW_WRITE_OFTEN;

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
    }

    // config libcamera with 1 stream
    libcamera::StreamConfiguration cfg;
    cfg.bufferCount = LIBCAM_STREAM_BUFNUM;
    cfg.size.width = m_libcamera_stream_width;
    cfg.size.height = m_libcamera_stream_height;
    cfg.pixelFormat = HalFromat2PixelFormat(m_libcamera_stream_format);
    camCfg->addConfiguration(cfg);

    int ret = camera_->configure(camCfg.get());
    if (ret) {
        ALOGE("%s: Failed to configure camera %s", __func__, camera_->id().c_str());
        return ret;
    }

    std::set<libcamera::Stream *> libCameraStreamSet = camera_->streams();
    ALOGI("%s: libCameraStreamSet size %lu, stream_num %d", __func__, libCameraStreamSet.size(),
          stream_num);
#if 0
    if (libCameraStreamSet.size() != 1) {
        ALOGE("%s: libCameraStreamSet size %d, should be 1", __func__, libCameraStreamSet.size());
        return BAD_VALUE;
    }
#endif

    mLibCameraStream = *(libCameraStreamSet.begin());
    ALOGI("%s: mLibCameraStream %p", __func__, mLibCameraStream);

    // allocate libcamera frame buffers
    for (int i = 0; i < LIBCAM_STREAM_BUFNUM; i++) {
        uint32_t bufferStride;
        buffer_handle_t hnd;
        // ??? fix me
        uint64_t usage = GRALLOC_USAGE_HW_CAMERA_WRITE | GRALLOC_USAGE_SW_READ_OFTEN |
                GRALLOC_USAGE_PRIVATE_3;
        auto status = GraphicBufferAllocator::get().allocate(m_libcamera_stream_width,
                                                             m_libcamera_stream_height,
                                                             m_libcamera_stream_format,
                                                             /*layerCount=*/1, usage, &hnd,
                                                             &bufferStride, "NxpCamera");
        if (status != ::android::OK) {
            ALOGE("%s: failed to allocate buffer:%d x %d, format=%x, usage=%lx, ret=%d", __func__,
                  m_libcamera_stream_width, m_libcamera_stream_height, m_libcamera_stream_format,
                  usage, status);
            return BAD_VALUE;
        }

        std::unique_ptr<libcamera::FrameBuffer> frameBuffer =
                CreateFrameBuffer(hnd, mLibCameraStream->configuration());
        if (frameBuffer == nullptr) {
            ALOGE("%s, CreateFrameBuffer faliled, hnd %p, index %d", __func__, hnd, i);
            return BAD_VALUE;
        }

        libcamera::FrameBuffer *pfb = frameBuffer.get();

        mFrameBufferHandleMap[pfb] = hnd;
        mFrameBuffersFree.push_back(std::move(frameBuffer));
        ALOGV("%s: mFrameBufferHandleMap[%p] %p, mFrameBuffersFree size %lu, this %p", __func__, pfb,
              hnd, mFrameBuffersFree.size(), this);
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

    /* free buffers in mFrameBuffersFree */
    if (mFrameBuffersFree.size() != LIBCAM_STREAM_BUFNUM)
        ALOGW("%s: !!! unexpected, mFrameBuffersFree size %lu != %d", __func__, mFrameBuffersFree.size(),
              LIBCAM_STREAM_BUFNUM);

    for (auto &it : mFrameBuffersFree) {
        libcamera::FrameBuffer *frameBuffer = it.get();
        auto iter = mFrameBufferHandleMap.find(frameBuffer);
        if (iter == mFrameBufferHandleMap.end()) {
            ALOGW("%s: !!! unexpected, mFrameBufferHandleMap has no key %p", __func__, frameBuffer);
            continue;
        }

        buffer_handle_t hnd = mFrameBufferHandleMap[frameBuffer];
        GraphicBufferAllocator::get().free(hnd);
        ALOGI("%s: mFrameBufferHandleMap[%p] %p, this %p", __func__, frameBuffer, hnd, this);

        it.reset();
    }

    // Should no buffers in mFrameBuffersBusy. Even so, don't free them since may cause exception.
    // It's the unexpected case, give warning and should refine code.
    if (!mFrameBuffersBusy.empty())
        ALOGW("%s: !!! unexpected, mFrameBuffersBusy size %lu != 0", __func__, mFrameBuffersBusy.size());

    mFrameBuffersFree.clear();
    mFrameBuffersBusy.clear();
    mFrameBufferHandleMap.clear();

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
            plansInfo.num = 2;
            plansInfo.plans[0].offset = 0;
            plansInfo.plans[0].size = width * height;
            plansInfo.plans[1].offset = plansInfo.plans[0].size;
            plansInfo.plans[1].size = width * height / 2;
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
        libcamera::SharedFD fd{hnd->data[0]};
        if (!fd.isValid()) {
            ALOGE("%s: No valid fd %d", __func__, hnd->data[i]);
            return nullptr;
        }

        planes[i].fd = fd;
        planes[i].offset = plansInfo.plans[i].offset;
        planes[i].length = plansInfo.plans[i].size;

        ALOGI("%s:, plan %lu, fd %d, offset %d, length %d", __func__, i, fd.get(), planes[i].offset,
              planes[i].length);
    }

    return std::make_unique<libcamera::FrameBuffer>(std::move(planes));
}

Stream *CameraDeviceSessionHwlImpl::GetStreamById(int32_t stream_id, PipelineInfo *pInfo) {
    if (pInfo == NULL)
        return NULL;

    uint32_t stream_num = pInfo->streams->size();
    for (uint32_t i = 0; i < stream_num; i++) {
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

    Mutex::Autolock _l(mLock);
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

        // Record fence fd
        uint32_t outBufNum = requests[i].output_buffers.size();
        frame_request->at(i).outBufferFences.resize(outBufNum);
        for (int j = 0; j < (int)outBufNum; j++) {
            FenceFdInfo fenceInfo = {-1, -1};
            fenceInfo.acquire_fence_fd =
                    importFence(requests[i].output_buffers[j].acquire_fence, mDebug);
            ALOGV("%s, acquire_fence_fd %d", __func__, fenceInfo.acquire_fence_fd);
            frame_request->at(i).outBufferFences[j] = fenceInfo;
        }

        uint32_t waitMs = 0;
        while (mFrameBuffersFree.empty()) {
            // unlock, so the in requestComplete() thread, has chance to call
            // mFrameBuffersFree.push_back().
            mLock.unlock();
            ALOGW("%s: mFrameBuffersFree empty, wait %d ms", __func__, WAIT_ITVL_MS);
            usleep(WAIT_ITVL_US);
            waitMs += WAIT_ITVL_MS;
            mLock.lock();
            if (waitMs > 500) {
                ALOGE("%s: mFrameBuffersFree still empty, wait 500ms", __func__);
                return BAD_VALUE;
            }
        }

        std::unique_ptr<libcamera::FrameBuffer> uptrFrameBuffer =
                std::move(mFrameBuffersFree.front());
        libcamera::FrameBuffer *frameBuffer = uptrFrameBuffer.get();
        mFrameBuffersFree.pop_front();
        mFrameBuffersBusy.push_back(std::move(uptrFrameBuffer));
        if (mDebug)
            ALOGI("%s: mFrameBuffersFree size %lu, mFrameBuffersBusy size %lu", __func__,
                  mFrameBuffersFree.size(), mFrameBuffersBusy.size());

        frame_request->at(i).request =
                camera_->createRequest(reinterpret_cast<uint64_t>(&frame_request->at(i)));
        int ret = frame_request->at(i).request->addBuffer(mLibCameraStream, frameBuffer);
        if (ret) {
            ALOGE("%s, request->addBuffer failed, ret %d", __func__, ret);
            return ret;
        }

        // ISPProcess
        libcamera::Request *request = frame_request->at(i).request.get();
        libcamera::ControlList &controls = request->controls();
        // sequence and controls.size are always 0, that is, the control commands received from
        // HwlPipelineRequest
        if (mDebug)
            ALOGI("==== %s: sequence %d, controls size %zu", __func__, request->sequence(),
                  controls.size());
        m_IspWrapper->process((HalCameraMetadata *)(requests[i].settings.get()), controls);

        ret = camera_->queueRequest(frame_request->at(i).request.get());
        if (ret) {
            ALOGE("%s, camera_->queueRequest failed, ret %d", __func__, ret);
            return ret;
        }
    }

    map_frame_request[frame_number] = frame_request;
    // DumpRequest();
    mInQueRequestIdx++;

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

static void DumpStream(void *src, uint32_t srcSize, void *dst, uint32_t dstSize, int32_t id) {
    char value[PROPERTY_VALUE_MAX];
    int fdSrc = -1;
    int fdDst = -1;
    int32_t streamIdBitVal = 0;

    if ((src == NULL) || (srcSize == 0) || (dst == NULL) || (dstSize == 0))
        return;

    property_get("vendor.rw.camera.test", value, "");
    if (strcmp(value, "") == 0)
        return;

    streamIdBitVal = atoi(value);
    if ((streamIdBitVal & (1 << id)) == 0)
        return;

    ALOGI("%s: src size %d, dst size %d, stream id %d", __func__, srcSize, dstSize, id);

    char srcFile[32];
    char dstFile[32];

    snprintf(srcFile, 32, "/data/%d-src.data", id);
    srcFile[31] = 0;
    snprintf(dstFile, 32, "/data/%d-dst.data", id);
    dstFile[31] = 0;

    fdSrc = open(srcFile, O_CREAT | O_APPEND | O_WRONLY, S_IRWXU | S_IRWXG);
    fdDst = open(dstFile, O_CREAT | O_APPEND | O_WRONLY, S_IRWXU | S_IRWXG);

    if ((fdSrc < 0) || (fdDst < 0)) {
        ALOGW("%s: file open error, srcFile: %s, fd %d, dstFile: %s, fd %d", __func__, srcFile,
              fdSrc, dstFile, fdDst);
        return;
    }

    write(fdSrc, src, srcSize);
    write(fdDst, dst, dstSize);

    close(fdSrc);
    close(fdDst);

    return;
}

status_t CameraDeviceSessionHwlImpl::ProcessCapbuf2Outbuf(ImxStreamBuffer *srcBuf,
                                                          StreamBuffer &output_buffers,
                                                          FenceFdInfo &outFences,
                                                          CameraMetadata &requestMeta) {
    int ret = 0;
    bool isSkipHandle = false;
    if (srcBuf == NULL)
        return BAD_VALUE;

    StreamBuffer *it = &output_buffers;

    // If fence is valid, wait.  Ref the usage in EmulatedRequestProcessor.cpp.
    int acquire_fence_fd = outFences.acquire_fence_fd;
    if (acquire_fence_fd > -1) {
        ALOGV("%s, before sync_wait fence fd %d", __func__, acquire_fence_fd);
        ret = sync_wait(acquire_fence_fd, CAMERA_SYNC_TIMEOUT);
        ALOGV("%s, after sync_wait fence fd %d", __func__, acquire_fence_fd);
        closeFence(acquire_fence_fd, mDebug);
        outFences.acquire_fence_fd = -1;
        if (ret != OK) {
            ALOGW("%s: Timeout waiting on acquire fence %d, on stream %d, buffer %lu", __func__,
                  acquire_fence_fd, it->stream_id, it->buffer_id);
        }
    }

    Stream *pStream = GetStreamFromStreamBuffer(it);
    if (pStream == NULL) {
        ALOGE("%s, dst buf belong to stream %d, but the stream is not configured", __func__,
              it->stream_id);
        return BAD_VALUE;
    }

    ImxStreamBuffer *dstBuf = CreateImxStreamBufferFromBufferHandle(it->buffer, pStream);
    if (dstBuf == NULL)
        return BAD_VALUE;

    uint64_t t1 = systemTime();

    if (dstBuf->mStream->format() == HAL_PIXEL_FORMAT_BLOB) {
        mJpegBuilder->reset();
        mJpegBuilder->setMetadata(&requestMeta);
        processJpegBuffer(srcBuf, dstBuf, &requestMeta);
    } else {
        processFrameBuffer(srcBuf, dstBuf, &requestMeta);
    }

    uint64_t t2 = systemTime();

    if (mDebug) {
        ALOGI("%s: use %lu ms, src: virt %p, phy 0x%lx, size %dx%d, format 0x%x, dst: virt %p, phy "
              "0x%lx, size %dx%d, format 0x%x, mCamBlitCopyType %d, mCamBlitCscType %d",
              __func__, (t2 - t1) / 1000000, srcBuf->mVirtAddr, srcBuf->mPhyAddr,
              srcBuf->mStream->width(), srcBuf->mStream->height(), srcBuf->mStream->format(),
              dstBuf->mVirtAddr, dstBuf->mPhyAddr, dstBuf->mStream->width(),
              dstBuf->mStream->height(), dstBuf->mStream->format(), mCamBlitCopyType,
              mCamBlitCscType);
    }

    DumpStream(srcBuf->mVirtAddr, srcBuf->mFormatSize, dstBuf->mVirtAddr, dstBuf->mFormatSize,
               dstBuf->mStream->id());

    ReleaseImxStreamBuffer(dstBuf);
    return 0;
}

status_t CameraDeviceSessionHwlImpl::ProcessCapbuf2MultiOutbuf(
        ImxStreamBuffer *srcBuf, std::vector<StreamBuffer> &output_buffers,
        std::vector<FenceFdInfo> &outFences, CameraMetadata &requestMeta) {
    int ret = 0;

    if (srcBuf == NULL)
        return BAD_VALUE;

    int outBufSize = output_buffers.size();
    int outFencesSize = outFences.size();

    if (outBufSize != outFencesSize) {
        ALOGE("%s: outBufSize(%d) != outFencesSize(%d)", __func__, outBufSize, outFencesSize);
        return BAD_VALUE;
    }

    for (int i = 0; i < outBufSize; i++) {
        ImxStreamBuffer *srcBufTmp = srcBuf;
        int sameResIdx = -1;
        int sameResFmtIdx = -1;
        Stream *pStreamSameRes = NULL;
        Stream *pStreamSameResFmt = NULL;

        // found if there's same res/fmt or same res processed output buffer.
        if (i > 0) {
            Stream *pCurStream = GetStreamFromStreamBuffer(&output_buffers[i]);
            for (int j = 0; j < i; j++) {
                Stream *pPreStream = GetStreamFromStreamBuffer(&output_buffers[j]);
                if ((pCurStream == NULL) || (pPreStream == NULL)) {
                    ALOGE("%s: unexpected, pCurStream %p, idx %d,  pPreStream %p, idx %d", __func__,
                          pCurStream, i, pPreStream, j);
                    return BAD_VALUE;
                }

                if ((pCurStream->width == pPreStream->width) &&
                    (pCurStream->height == pPreStream->height)) {
                    sameResIdx = j;
                    pStreamSameRes = pPreStream;
                    if (pCurStream->format == pPreStream->format) {
                        sameResFmtIdx = j;
                        pStreamSameResFmt = pPreStream;
                    }
                }
            }

            if (mDebug)
                ALOGI("%s: current output buffer idx %d, sameResIdx %d, sameResFmtIdx %d", __func__, i,
                      sameResIdx, sameResFmtIdx);

            if (sameResFmtIdx >= 0)
                srcBufTmp =
                        CreateImxStreamBufferFromBufferHandle(output_buffers[sameResFmtIdx].buffer,
                                                              pStreamSameResFmt);
            else if (sameResIdx >= 0)
                srcBufTmp = CreateImxStreamBufferFromBufferHandle(output_buffers[sameResIdx].buffer,
                                                                  pStreamSameRes);
            else
                srcBufTmp = srcBuf;
        }

        int status = ProcessCapbuf2Outbuf(srcBufTmp, output_buffers[i], outFences[i], requestMeta);
        if (status)
            ret = BAD_VALUE;

        if (srcBufTmp != srcBuf)
            ReleaseImxStreamBuffer(srcBufTmp);
    }

    return ret;
}

uint64_t CameraDeviceSessionHwlImpl::GetTimestamp(libcamera::Request *request) {
    Mutex::Autolock _l(mLock);
    libcamera::Request::BufferMap bufMap = request->buffers();
    for (auto &t : bufMap) {
        libcamera::FrameBuffer *frameBuffer = t.second;
        if (frameBuffer)
            return frameBuffer->metadata().timestamp;
    }

    ALOGW("!!! %s: unexpected, no valid frameBuffer in bufMap, size %lu", __func__, bufMap.size());

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
    uint64_t timestamp_ns = readout_timestamp_ns - 33333333;
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
        libcamera::ControlList &metadata = request->metadata();
        ALOGI("==== %s: frame %d, output_buffers %lu, result->regsult_metadata %p, entry count %d, libcamera::Request buffers %lu, sequence %u, metadata size %lu",
              __func__, frame, result->output_buffers.size(), result->result_metadata.get(),
              (int)result->result_metadata->GetEntryCount(), request->buffers().size(),
              request->sequence(), metadata.size());
        for (libcamera::ControlList::iterator it = metadata.begin(); it != metadata.end(); it++) {
            int i = it->first;
            libcamera::ControlValue ctlVal = it->second;
            ALOGI("==== meta id: %d, val: %s, type %d, numElements %zu", i,
                  ctlVal.toString().c_str(), ctlVal.type(), ctlVal.numElements());
        }
    }

    libcamera::FrameBuffer *frameBuffer = request->findBuffer(mLibCameraStream);
    buffer_handle_t hnd = mFrameBufferHandleMap[frameBuffer];
    uint64_t usage = GRALLOC_USAGE_PRIVATE_3 | GRALLOC_USAGE_HW_CAMERA_WRITE;
    Stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.width = m_libcamera_stream_width;
    stream.height = m_libcamera_stream_height;
    stream.format = m_libcamera_stream_format;
    stream.usage = usage;
    stream.id = 0;
    ImxStreamBuffer *srcBuf = CreateImxStreamBufferFromBufferHandle(hnd, &stream);
    if (srcBuf == NULL) {
        ALOGE("%s: CreateImxStreamBufferFromBufferHandle failed, hnd %p, res %dx%d", __func__, hnd,
              stream.width, stream.height);
        return;
    }

    ProcessCapbuf2MultiOutbuf(srcBuf, hwReq->output_buffers, frameRequest->outBufferFences,
                              requestMeta);

    ReleaseImxStreamBuffer(srcBuf);

    HandleMetaLocked(result->result_metadata, timestamp_ns);

    // call back to process result
    if (pInfo->pipeline_callback.process_pipeline_result) {
        pInfo->pipeline_callback.process_pipeline_result(std::move(result));
    }

    if (mDebug)
        ItvlStat(mPreHandleImageTime, (char *)"requestComplete(), process_pipeline_result");

    Mutex::Autolock _l(mLock);
    std::unique_ptr<libcamera::FrameBuffer> uptrFrameBuffer = std::move(mFrameBuffersBusy.front());
    libcamera::FrameBuffer *frameBufferFront = uptrFrameBuffer.get();
    mFrameBuffersBusy.pop_front();
    mFrameBuffersFree.push_back(std::move(uptrFrameBuffer));

    if (mDebug)
        ALOGI("%s: mFrameBuffersFree size %lu, mFrameBuffersBusy size %lu", __func__,
              mFrameBuffersFree.size(), mFrameBuffersBusy.size());

    if (frameBuffer != frameBufferFront)
        ALOGW("%s: !!! frameBuffer %p != %p, the front of mFrameBuffersBusy", __func__, frameBuffer,
              frameBufferFront);

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
