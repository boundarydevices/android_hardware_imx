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

#define LOG_TAG "CameraDeviceSessionHwlImpl"

#include "CameraUtils.h"
#include "CameraMetadata.h"
#include "CameraDeviceSessionHWLImpl.h"
#include "ISPCameraDeviceHWLImpl.h"
#include "ImageProcess.h"

#include <hardware/gralloc.h>
#include <inttypes.h>
#include <log/log.h>
#include <utils/Trace.h>
#include <binder/MemoryHeapBase.h>
#include <hardware/camera3.h>

//using namespace fsl;
using namespace cameraconfigparser;

namespace android {

std::unique_ptr<CameraDeviceSessionHwlImpl>
CameraDeviceSessionHwlImpl::Create(
    uint32_t camera_id, CameraMetadata *pMeta, CameraDeviceHwlImpl *pDev)
{
    if (pMeta == nullptr) {
        return nullptr;
    }

    auto session = std::unique_ptr<CameraDeviceSessionHwlImpl>(
        new CameraDeviceSessionHwlImpl());
    if (session == nullptr) {
        ALOGE("%s: Creating CameraDeviceSessionHwlImpl failed",
              __func__);
        return nullptr;
    }

    status_t res = session->Initialize(camera_id, pMeta, pDev);
    if (res != OK) {
        ALOGE("%s: session->Initialize  failed: %s(%d)",
              __func__,
              strerror(-res),
              res);
        return nullptr;
    }

    return session;
}

status_t CameraDeviceSessionHwlImpl::Initialize(
    uint32_t camera_id, CameraMetadata *pMeta, CameraDeviceHwlImpl *pDev)
{
    int ret;
    camera_id_ = camera_id;
    m_meta = pMeta;

    if ((pMeta == NULL) || (pDev == NULL))
        return BAD_VALUE;

    ALOGI("Initialize, meta %p, entry count %d", m_meta->GetStaticMeta(), (int)m_meta->GetStaticMeta()->GetEntryCount());

    CameraSensorMetadata *cam_metadata = &(pDev->mSensorData);

    ALOGI("%s: create video stream for camera %s, buffer type %d, path %s",
          __func__,
          cam_metadata->camera_name,
          cam_metadata->buffer_type,
          pDev->mDevPath);

    if (strstr(cam_metadata->camera_name, UVC_NAME))
        pVideoStream = new UvcStream(pDev->mDevPath, this);
    else if(strstr(cam_metadata->camera_name, ISP_SENSOR_NAME)) {
        pVideoStream = new ISPCameraMMAPStream(this);
        ((ISPCameraMMAPStream *)pVideoStream)->createISPWrapper(pDev->mDevPath, &mSensorData);
    }
    else if (cam_metadata->buffer_type == CameraSensorMetadata::kMmap)
        pVideoStream = new MMAPStream(this);
    else if (cam_metadata->buffer_type == CameraSensorMetadata::kDma)
        pVideoStream = new DMAStream((bool)cam_metadata->mplane, this);
    else {
        ALOGE("%s: unsupported camera %s, or unsupported buffer type %d", __func__, cam_metadata->camera_name, cam_metadata->buffer_type);
        return BAD_VALUE;
    }

    ALOGI("%s: pVideoStream %p created", __func__, pVideoStream);

    if (pVideoStream == NULL)
        return BAD_VALUE;

    ret = pVideoStream->openDev(pDev->mDevPath);
    if (ret) {
        ALOGE("pVideoStream->openDev failed, ret %d", ret);
        return BAD_VALUE;
    }

    pMemManager = fsl::MemoryManager::getInstance();
    if (pMemManager == NULL) {
        ALOGE("%s, unexpected, pMemManager is null !!!", __func__);
        return BAD_VALUE;
    }

    // create jpeg builder
    mJpegBuilder = new JpegBuilder();

    // create work thread
    mWorkThread = new WorkThread(this);
    if (mWorkThread == NULL) {
        ALOGI("%s, new WorkThread failed", __func__);
        return BAD_VALUE;
    }

    // Device may be destroyed after create session, need copy some members from device.
    mCamBlitCopyType = pDev->mCamBlitCopyType;
    mCamBlitCscType = pDev->mCamBlitCscType;
    memcpy(mJpegHw, pDev->mJpegHw, JPEG_HW_NAME_LEN);
    mSensorData = pDev->mSensorData;

    mPreviewResolutionCount = pDev->mPreviewResolutionCount;
    memcpy(mPreviewResolutions, pDev->mPreviewResolutions, MAX_RESOLUTION_SIZE*sizeof(int));
    mPictureResolutionCount = pDev->mPictureResolutionCount;
    memcpy(mPictureResolutions, pDev->mPictureResolutions, MAX_RESOLUTION_SIZE*sizeof(int));

    strncpy(mDevPath, pDev->mDevPath, CAMAERA_FILENAME_LENGTH);
    mDevPath[CAMAERA_FILENAME_LENGTH - 1] = 0;

    return OK;
}

CameraDeviceSessionHwlImpl::CameraDeviceSessionHwlImpl()
{
    ALOGI("%s: this %p", __func__, this);

    memset(&m3aState, 0, sizeof(m3aState));

    pVideoStream = NULL;
    pMemManager = NULL;
    m_meta = NULL;
    mSettings = NULL;
}

CameraDeviceSessionHwlImpl::~CameraDeviceSessionHwlImpl()
{
    ALOGI("%s: this %p, %p, %p", __func__, this, mWorkThread.get(), &mWorkThread);

    if(mWorkThread != NULL) {
        mWorkThread->requestExitAndWait();
        ALOGI("%s, mWorkThread exited", __func__);
    }

    if (pVideoStream) {
        pVideoStream->Stop();
        pVideoStream->closeDev();
        delete pVideoStream;
    }

    if (m_meta)
        delete m_meta;

    if (mJpegBuilder != NULL)
        mJpegBuilder.clear();

    if(mSettings != NULL)
        mSettings.reset();
}

#define WAIT_TIME_OUT 100000000LL  // unit ns, wait 100ms
int CameraDeviceSessionHwlImpl::HandleRequest()
{
    Mutex::Autolock _l(mLock);

    if(!pipelines_built_) {
        ALOGV("%s: pipeline not built", __func__);
        usleep(200);
        return OK;
    }

    if (map_frame_request.empty()) {
        ALOGV("map_frame_request empty, wait");
        mCondition.waitRelative(mLock, WAIT_TIME_OUT);
    }

    if (map_frame_request.empty()) {
        ALOGW("map_frame_request still empty after %lld ns", WAIT_TIME_OUT);
        return OK;
    }

    for (auto it = map_frame_request.begin(); it != map_frame_request.end(); it++) {
        uint32_t frame = it->first;
        std::vector<HwlPipelineRequest> *request = it->second;
        uint32_t reqNum = request->size();

        ALOGV("%s: frame %d, request num %d", __func__, frame, reqNum);

        for (uint32_t i = 0; i < reqNum; i++) {
            HwlPipelineRequest *hwReq = &(request->at(i));
            uint32_t pipeline_id = hwReq->pipeline_id;
            ALOGV("request: pipeline_id %d, output buffer num %d, meta %p",
                  (int)pipeline_id,
                  (int)hwReq->output_buffers.size(),
                  hwReq->settings.get());

            PipelineInfo *pInfo = map_pipeline_info[pipeline_id];
            if(pInfo == NULL) {
                ALOGW("Unexpected, pipeline %d is invalid", pipeline_id);
                continue;
            }

            // notify shutter
            uint64_t timestamp = systemTime();

            if (pInfo->pipeline_callback.notify) {
                NotifyMessage msg{
                    .type = MessageType::kShutter,
                    .message.shutter = {
                        .frame_number = frame,
                        .timestamp_ns = timestamp}};

                pInfo->pipeline_callback.notify(pipeline_id, msg);
            }

           // save the latest meta
           if (hwReq->settings != NULL) {
                if(mSettings != NULL)
                    mSettings.reset();

                mSettings = HalCameraMetadata::Clone(hwReq->settings.get());
            }

            pVideoStream->ISPProcess(mSettings.get());

            auto result = std::make_unique<HwlPipelineResult>();
            if (mSettings != NULL)
                result->result_metadata = HalCameraMetadata::Clone(mSettings.get());
            else
                result->result_metadata = HalCameraMetadata::Create(1, 10);

            // process frame
            CameraMetadata requestMeta(result->result_metadata.get());
            HandleFrameLocked(hwReq->output_buffers, requestMeta);

            // return result
            result->camera_id = camera_id_;
            result->pipeline_id = pipeline_id;
            result->frame_number = frame;
            result->partial_result = 1;
            result->output_buffers.assign(hwReq->output_buffers.begin(), hwReq->output_buffers.end());
            result->input_buffers.reserve(0);
            result->physical_camera_results.reserve(0);

            ALOGV("result->result_metadata %p, entry count %d",
                  result->result_metadata.get(),
                  (int)result->result_metadata->GetEntryCount());

            HandleMetaLocked(result->result_metadata, timestamp);

            // call back to process result
            if (pInfo->pipeline_callback.process_pipeline_result)
                pInfo->pipeline_callback.process_pipeline_result(std::move(result));
        }
    }

    CleanRequestsLocked();

    return OK;
}

status_t CameraDeviceSessionHwlImpl::HandleFrameLocked(std::vector<StreamBuffer> output_buffers, CameraMetadata &requestMeta)
{
    int ret = 0;
    ImxStreamBuffer *pImxStreamBuffer;

    pImxStreamBuffer = pVideoStream->onFrameAcquireLocked();
    if (pImxStreamBuffer == NULL) {
        ALOGW("onFrameAcquireLocked failed");
        usleep(5000);
        return 0;
    }

    ret = ProcessCapturedBuffer(pImxStreamBuffer, output_buffers, requestMeta);

    pVideoStream->onFrameReturnLocked(*pImxStreamBuffer);

    return ret;
}

ImxStreamBuffer *CameraDeviceSessionHwlImpl::CreateImxStreamBufferFromStreamBuffer(StreamBuffer *buf, Stream *stream)
{
    void *pBuf = NULL;
    fsl::Memory *handle = NULL;

    if ((buf == NULL) || (buf->buffer == NULL) || (stream == NULL))
        return NULL;

    ImxStreamBuffer *imxBuf = new ImxStreamBuffer();
    if (imxBuf == NULL)
        return NULL;

    handle = (fsl::Memory *)(buf->buffer);
    pMemManager->lock(handle, handle->usage, 0, 0, handle->width, handle->height, &pBuf);

    imxBuf->mVirtAddr = pBuf;
    imxBuf->mPhyAddr = handle->phys;
    imxBuf->mSize = handle->size;
    imxBuf->buffer = buf->buffer;
    imxBuf->mFormatSize = getSizeByForamtRes(handle->format, stream->width, stream->height, false);
    if(imxBuf->mFormatSize == 0)
        imxBuf->mFormatSize = imxBuf->mSize;

    ALOGV("%s, buffer: virt %p, phy 0x%llx, size %d, format 0x%x, acquire_fence %p, release_fence %p, stream: res %dx%d, format 0x%x, size %d",
          __func__,
          imxBuf->mVirtAddr,
          imxBuf->mPhyAddr,
          imxBuf->mSize,
          handle->format,
          buf->acquire_fence,
          buf->release_fence,
          stream->width,
          stream->height,
          stream->format,
          stream->buffer_size);

    imxBuf->mStream = new ImxStream(stream->width, stream->height, handle->format, stream->usage, stream->id);

    if (imxBuf->mStream == NULL)
        goto error;

    goto finish;

error:
    if (imxBuf)
        free(imxBuf);

    if (pBuf)
        pMemManager->unlock(handle);

    return NULL;

finish:
    return imxBuf;
}

void CameraDeviceSessionHwlImpl::ReleaseImxStreamBuffer(ImxStreamBuffer *imxBuf)
{
    if (imxBuf == NULL)
        return;

    if (imxBuf->mStream)
        delete (imxBuf->mStream);

    fsl::Memory *handle = (fsl::Memory *)(imxBuf->buffer);
    pMemManager->unlock(handle);

    delete imxBuf;
}

Stream *CameraDeviceSessionHwlImpl::GetStreamFromStreamBuffer(StreamBuffer *buf)
{
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

static void DumpStream(void *src, uint32_t srcSize, void *dst, uint32_t dstSize, int32_t id)
{
    char value[PROPERTY_VALUE_MAX];
    int  fdSrc = -1;
    int fdDst = -1;
    int32_t streamIdBitVal = 0;

    if ((src == NULL) || (srcSize == 0) || (dst == NULL) || (dstSize == 0))
        return;


    property_get("vendor.rw.camera.test", value, "");
    if (strcmp(value, "") == 0)
        return;

    streamIdBitVal = atoi(value);
    if ((streamIdBitVal & (1<<id)) == 0)
        return;

    ALOGI("%s: src size %d, dst size %d, stream id %d", __func__, srcSize, dstSize, id);

    char srcFile[32];
    char dstFile[32];

    snprintf(srcFile, 32, "/data/%d-src.data", id);
    srcFile[31] = 0;
    snprintf(dstFile, 32, "/data/%d-dst.data", id);
    dstFile[31] = 0;

    fdSrc = open(srcFile, O_CREAT|O_APPEND|O_WRONLY, S_IRWXU|S_IRWXG);
    fdDst = open(dstFile, O_CREAT|O_APPEND|O_WRONLY, S_IRWXU|S_IRWXG);

    if ((fdSrc < 0) || (fdDst < 0)) {
        ALOGW("%s: file open error, srcFile: %s, fd %d, dstFile: %s, fd %d",
           __func__, srcFile, fdSrc, dstFile, fdDst);
        return;
    }

    write(fdSrc, src, srcSize);
    write(fdDst, dst, dstSize);

    close(fdSrc);
    close(fdDst);

    return;
}

status_t CameraDeviceSessionHwlImpl::ProcessCapturedBuffer(ImxStreamBuffer *srcBuf, std::vector<StreamBuffer> output_buffers, CameraMetadata &requestMeta)
{
    int ret = 0;

    if (srcBuf == NULL)
        return BAD_VALUE;

    for (auto it : output_buffers) {
        Stream *pStream = GetStreamFromStreamBuffer(&it);
        if (pStream == NULL) {
            ALOGE("%s, dst buf belong to stream %d, but the stream is not configured", __func__, it.stream_id);
            return BAD_VALUE;
        }

        ImxStreamBuffer *dstBuf = CreateImxStreamBufferFromStreamBuffer(&it, pStream);
        if (dstBuf == NULL)
            return BAD_VALUE;

        if (dstBuf->mStream->format() == HAL_PIXEL_FORMAT_BLOB) {
            mJpegBuilder->reset();
            mJpegBuilder->setMetadata(&requestMeta);

            ret = processJpegBuffer(srcBuf, dstBuf, &requestMeta);
        } else
            processFrameBuffer(srcBuf, dstBuf, &requestMeta);

        DumpStream(srcBuf->mVirtAddr, srcBuf->mFormatSize, dstBuf->mVirtAddr, dstBuf->mFormatSize, dstBuf->mStream->id());

        ReleaseImxStreamBuffer(dstBuf);
    }

    return 0;
}

int32_t CameraDeviceSessionHwlImpl::processFrameBuffer(ImxStreamBuffer *srcBuf, ImxStreamBuffer *dstBuf, CameraMetadata *meta)
{
    if ((srcBuf == NULL) || (dstBuf == NULL) || (meta == NULL)) {
        ALOGE("%s srcBuf %p, dstBuf %p, meta %p", __func__, srcBuf, dstBuf, meta);
        return BAD_VALUE;
    }

    fsl::ImageProcess *imageProcess = fsl::ImageProcess::getInstance();
    CscHw csc_hw;

    ImxStream *srcStream = srcBuf->mStream;
    ImxStream *dstStream = dstBuf->mStream;

    if (srcStream->mWidth == dstStream->mWidth &&
        srcStream->mHeight == dstStream->mHeight &&
        srcStream->format() == dstStream->format())
        csc_hw = mCamBlitCopyType;
    else
        csc_hw = mCamBlitCscType;

    return imageProcess->handleFrame(*dstBuf, *srcBuf, csc_hw);
}

int32_t CameraDeviceSessionHwlImpl::processJpegBuffer(ImxStreamBuffer *srcBuf, ImxStreamBuffer *dstBuf, CameraMetadata *meta)
{
    int32_t ret = 0;
    int32_t encodeQuality = 100, thumbQuality = 100;
    int32_t thumbWidth = 0, thumbHeight = 0;
    JpegParams *mainJpeg = NULL, *thumbJpeg = NULL;
    void *rawBuf = NULL, *thumbBuf = NULL;
    uint8_t *pDst = NULL;
    struct camera3_jpeg_blob *jpegBlob = NULL;
    uint32_t bufSize = 0;
    int maxJpegSize = mSensorData.maxjpegsize;

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
            ALOGE("Error: %s format not supported", __func__);
    }

    sp<MemoryHeapBase> rawFrame(
        new MemoryHeapBase(captureSize, 0, "rawFrame"));
    rawBuf = rawFrame->getBase();
    if (rawBuf == MAP_FAILED) {
        ALOGE("%s new MemoryHeapBase failed", __func__);
        return BAD_VALUE;
    }

    sp<MemoryHeapBase> thumbFrame(
        new MemoryHeapBase(captureSize, 0, "thumbFrame"));
    thumbBuf = thumbFrame->getBase();
    if (thumbBuf == MAP_FAILED) {
        ALOGE("%s new MemoryHeapBase failed", __func__);
        return BAD_VALUE;
    }

    mainJpeg = new JpegParams((uint8_t *)srcBuf->mVirtAddr,
                              (uint8_t *)(uintptr_t)srcBuf->mPhyAddr,
                              srcBuf->mSize,
                              (uint8_t *)rawBuf,
                              captureSize,
                              encodeQuality,
                              srcStream->mWidth,
                              srcStream->mHeight,
                              capture->mWidth,
                              capture->mHeight,
                              srcStream->format());

    ret = meta->getJpegThumbSize(thumbWidth, thumbHeight);
    if (ret != NO_ERROR) {
        ALOGE("%s getJpegThumbSize failed", __func__);
    }

    if ((thumbWidth > 0) && (thumbHeight > 0)) {
        int thumbSize = captureSize;
        thumbJpeg = new JpegParams((uint8_t *)srcBuf->mVirtAddr,
                                   (uint8_t *)(uintptr_t)srcBuf->mPhyAddr,
                                   srcBuf->mSize,
                                   (uint8_t *)thumbBuf,
                                   thumbSize,
                                   thumbQuality,
                                   srcStream->mWidth,
                                   srcStream->mHeight,
                                   thumbWidth,
                                   thumbHeight,
                                   srcStream->format());
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

    jpegBlob = (struct camera3_jpeg_blob *)(pDst + bufSize -
                                            sizeof(struct camera3_jpeg_blob));
    jpegBlob->jpeg_blob_id = CAMERA3_JPEG_BLOB_ID;
    jpegBlob->jpeg_size = mJpegBuilder->getImageSize();

    ALOGI("%s, dstbuf size %d, %d, jpeg_size %d, max jpeg size %d",
          __func__,
          (int)dstBuf->mSize,
          captureSize,
          jpegBlob->jpeg_size,
          maxJpegSize);

err_out:
    if (mainJpeg != NULL) {
        delete mainJpeg;
    }

    if (thumbJpeg != NULL) {
        delete thumbJpeg;
    }

    return ret;
}

status_t CameraDeviceSessionHwlImpl::HandleMetaLocked(std::unique_ptr<HalCameraMetadata> &resultMeta, uint64_t timestamp)
{
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

status_t CameraDeviceSessionHwlImpl::ConfigurePipeline(
    uint32_t physical_camera_id, HwlPipelineCallback hwl_pipeline_callback, const StreamConfiguration &request_config, const StreamConfiguration & /*overall_config*/, uint32_t *pipeline_id)
{
    Mutex::Autolock _l(mLock);
    if (pipeline_id == nullptr) {
        ALOGE("%s pipeline_id is nullptr", __func__);
        return BAD_VALUE;
    }

    if (pipelines_built_) {
        ALOGE("%s Cannot configure pipelines after calling BuildPipelines()",
              __func__);
        return ALREADY_EXISTS;
    }

    bool bSupport = CameraDeviceHwlImpl::StreamCombJudge(request_config,
        mPreviewResolutions, mPreviewResolutionCount,
        mPictureResolutions, mPictureResolutionCount);

    if (bSupport == false) {
        ALOGI("%s: IsStreamCombinationSupported return false", __func__);
        return BAD_VALUE;
    }

    *pipeline_id = pipeline_id_;

    PipelineInfo *pipeline_info = (PipelineInfo *)calloc(1, sizeof(PipelineInfo));
    if (pipeline_info == NULL) {
        ALOGE("%s malloc pipeline_info failed", __func__);
        return BAD_VALUE;
    }

    pipeline_info->pipeline_id = pipeline_id_;
    pipeline_info->physical_camera_id = physical_camera_id;
    pipeline_info->pipeline_callback = hwl_pipeline_callback;

    int stream_num = request_config.streams.size();
    if (stream_num == 0) {
        free(pipeline_info);
        ALOGE("%s stream num 0", __func__);
        return BAD_VALUE;
    }

    pipeline_info->streams = new std::vector<Stream>();
    pipeline_info->hal_streams = new std::vector<HalStream>();

    if ((pipeline_info->streams == NULL) || (pipeline_info->hal_streams == NULL)) {
        ALOGE("%s: no memory, pipeline_info->streams %p, pipeline_info->hal_streams %p",
            __func__, pipeline_info->streams, pipeline_info->hal_streams);
        return BAD_VALUE;
    }

    pipeline_info->streams->assign(request_config.streams.begin(), request_config.streams.end());

    previewIdx = -1;
    stillcapIdx = -1;
    recordIdx = -1;
    callbackIdx = -1;

    for (int i = 0; i < stream_num; i++) {
        Stream stream = request_config.streams[i];
        ALOGI("%s, stream %d: id %d, type %d, res %dx%d, format 0x%x, usage 0x%llx, space 0x%x, rot %d, is_phy %d, phy_id %d, size %d",
              __func__,
              i,
              stream.id,
              stream.stream_type,
              stream.width,
              stream.height,
              stream.format,
              (unsigned long long)stream.usage,
              stream.data_space,
              stream.rotation,
              stream.is_physical_camera_stream,
              stream.physical_camera_id,
              stream.buffer_size);

        HalStream hal_stream;
        memset(&hal_stream, 0, sizeof(hal_stream));
        int usage = 0;

        switch (stream.format) {
            case HAL_PIXEL_FORMAT_BLOB:
                ALOGI("%s create capture stream", __func__);
                hal_stream.override_format = HAL_PIXEL_FORMAT_BLOB;
                hal_stream.max_buffers = NUM_CAPTURE_BUFFER;
                usage = CAMERA_GRALLOC_USAGE_JPEG;
                stillcapIdx = i;
                break;

            case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
                hal_stream.override_format = HAL_PIXEL_FORMAT_YCBCR_422_I;
                hal_stream.max_buffers = NUM_PREVIEW_BUFFER;
                usage = CAMERA_GRALLOC_USAGE;

                if (stream.usage & GRALLOC_USAGE_HW_VIDEO_ENCODER) {
                    ALOGI("%s create video recording stream", __func__);
                    hal_stream.override_format = HAL_PIXEL_FORMAT_YCBCR_420_888;
                    recordIdx = i;
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

        hal_stream.producer_usage = stream.usage | usage;
        hal_stream.consumer_usage = 0;
        hal_stream.id = stream.id;
        hal_stream.override_data_space = stream.data_space;

        pipeline_info->hal_streams->push_back(std::move(hal_stream));
    }

    map_pipeline_info[pipeline_id_] = pipeline_info;
    pipeline_id_++;

    return OK;
}

int CameraDeviceSessionHwlImpl::PickConfigStream(uint32_t pipeline_id, uint8_t intent)
{
    auto iter = map_pipeline_info.find(pipeline_id);
    if (iter == map_pipeline_info.end()) {
        ALOGE("%s: Unknown pipeline ID: %u", __func__, pipeline_id);
        return -1;
    }

    PipelineInfo *pipeline_info = iter->second;
    if((pipeline_info == NULL) || (pipeline_info->streams == NULL)){
        ALOGE("%s: pipeline_info or streams is NULL for id %d", __func__, pipeline_id);
        return -1;
    }

    ALOGI("%s, previewIdx %d, callbackIdx %d, stillcapIdx %d, recordIdx %d, intent %d",
        __func__, previewIdx, callbackIdx, stillcapIdx, recordIdx, intent);

    int configIdx = -1;
    if(intent == ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE)
        configIdx = stillcapIdx;

    if (configIdx == -1) {
        if (previewIdx >= 0)
            configIdx = previewIdx;
        else if (callbackIdx >= 0)
            configIdx = callbackIdx;
        else if (stillcapIdx >= 0)
            configIdx = stillcapIdx;
        else if (recordIdx >= 0)
            configIdx = recordIdx;
        else {
            ALOGE("%s, no stream found to config v4l2", __func__);
            return -1;
        }
    }

    ALOGI("choose stream index %d as config stream", configIdx);

    return configIdx;
}

status_t CameraDeviceSessionHwlImpl::GetConfiguredHalStream(
    uint32_t pipeline_id, std::vector<HalStream> *hal_streams) const
{
    // fix me, build error as
    // error: 'this' argument to member function 'lock' has type 'const android::Mutex', but function is not marked const
    // Mutex::Autolock _l(mLock);

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

status_t CameraDeviceSessionHwlImpl::BuildPipelines()
{
    int ret = 0;

    ALOGI("enter %s, pVideoStream %p", __func__, pVideoStream);

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

#define DESTROY_WAIT_MS 100
#define DESTROY_WAIT_US (uint32_t)(DESTROY_WAIT_MS*1000)
void CameraDeviceSessionHwlImpl::DestroyPipelines()
{
    ALOGI("enter %s", __func__);

    Mutex::Autolock _l(mLock);
    if (!pipelines_built_) {
        // Not an error - nothing to destroy
        ALOGV("%s nothing to destroy", __func__);
        return;
    }

    /* If still has un-processed requests, wait some time to finish */
    if ( map_frame_request.empty() == false ) {
        ALOGW("%s: still has %d requests to process, wait %d ms", __func__, map_frame_request.size(), DESTROY_WAIT_MS);
        mLock.unlock();
        usleep(DESTROY_WAIT_US);
        mLock.lock();

        if( map_frame_request.empty() == false ) {
            ALOGW("%s: still has %d requests to process after wait, force clear", __func__, map_frame_request.size());
            map_frame_request.clear();
        }
    }

    /* clear  map_pipeline_info */
    for (auto it = map_pipeline_info.begin(); it != map_pipeline_info.end(); it++) {
        auto pInfo = it->second;
        if(pInfo == NULL) {
            ALOGW("%s, no pipeline info for id %u", __func__, it->first);
            continue;
        }

        if(pInfo->streams) {
            pInfo->streams->clear();
            delete pInfo->streams;
        }

        if(pInfo->hal_streams) {
            pInfo->hal_streams->clear();
            delete pInfo->hal_streams;
        }

        free(pInfo);
    }

    map_pipeline_info.clear();
    pipelines_built_ = false;
}

status_t CameraDeviceSessionHwlImpl::SubmitRequests(
    uint32_t frame_number, const std::vector<HwlPipelineRequest> &requests)
{
    Mutex::Autolock _l(mLock);

    int size = requests.size();
    std::vector<HwlPipelineRequest> *pipeline_request = new std::vector<HwlPipelineRequest>(size);

    for (int i = 0; i < size; i++) {
        uint32_t pipeline_id = requests[i].pipeline_id;
        ALOGV("%s, frame_number, request %d, pipeline_id %d, outbuffer num %d",
              __func__,
              frame_number,
              i,
              (int)pipeline_id,
              (int)requests[i].output_buffers.size());

        pipeline_request->at(i).pipeline_id = pipeline_id;
        pipeline_request->at(i).settings = HalCameraMetadata::Clone(requests[i].settings.get());
        pipeline_request->at(i).output_buffers.reserve(requests[i].output_buffers.size());
        pipeline_request->at(i).output_buffers.assign(requests[i].output_buffers.begin(), requests[i].output_buffers.end());

        pipeline_request->at(i).input_buffers.reserve(requests[i].input_buffers.size());
        pipeline_request->at(i).input_buffers.assign(requests[i].input_buffers.begin(), requests[i].input_buffers.end());
        pipeline_request->at(i).input_buffer_metadata.reserve(requests[i].input_buffer_metadata.size());

        // process capture intent
        uint8_t captureIntent = -1;
        int configIdx = -1;
        camera_metadata_ro_entry entry;
        int ret;

        if(requests[i].settings.get() == NULL)
            continue;

        ret = requests[i].settings->Get(ANDROID_CONTROL_CAPTURE_INTENT, &entry);
        if (ret != 0)
            continue;

        captureIntent = entry.data.u8[0];
        configIdx = PickConfigStream(pipeline_id, captureIntent);
        if(configIdx < 0)
            continue;

        uint32_t fps = 30;
        ret = requests[i].settings->Get(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, &entry);
        if ((ret == 0) && (entry.count > 1)) {
          if (entry.data.i32[0] <= 15 && entry.data.i32[1] <= 15)
          fps = 15;
        }

        PipelineInfo *pipeline_info = map_pipeline_info[pipeline_id];
        pVideoStream->SetBufferNumber(pipeline_info->hal_streams->at(configIdx).max_buffers + 1);

        // v4l2 hard code to use yuv422i. If in future other foramts are used, need refine code, maybe configed in json
        ret = pVideoStream->ConfigAndStart(HAL_PIXEL_FORMAT_YCbCr_422_I,
                                            pipeline_info->streams->at(configIdx).width,
                                            pipeline_info->streams->at(configIdx).height,
                                            fps);
        if (ret) {
            delete(pipeline_request);
            ALOGE("%s: pVideoStream->ConfigAndStart failed, ret %d", __func__, ret);
            return ret;
        }
    }

    map_frame_request[frame_number] = pipeline_request;
    mCondition.signal();

    return OK;
}

int CameraDeviceSessionHwlImpl::CleanRequestsLocked()
{
    if (map_frame_request.empty())
        return 0;

    for (auto it = map_frame_request.begin(); it != map_frame_request.end(); it++) {
        uint32_t frame = it->first;
        std::vector<HwlPipelineRequest> *request = it->second;
        if(request == NULL) {
            ALOGW("%s: frame %d request is null", __func__, frame);
            continue;
        }

        uint32_t reqNum = request->size();
        ALOGV("%s, map_frame_request, frame %d, reqNum %d", __func__, frame, reqNum);

        for (uint32_t i = 0; i < reqNum; i++) {
            HwlPipelineRequest *hwReq = &(request->at(i));
            uint32_t pipeline_id = hwReq->pipeline_id;
            PipelineInfo *pInfo = map_pipeline_info[pipeline_id];

            // clear hwReq
            hwReq->settings.reset();
            hwReq->output_buffers.clear();
            hwReq->input_buffers.clear();
            hwReq->input_buffer_metadata.clear();
        }

        request->clear();
        delete request;
    }

    map_frame_request.clear();

    return 0;
}

status_t CameraDeviceSessionHwlImpl::Flush()
{
    return OK;
}

uint32_t CameraDeviceSessionHwlImpl::GetCameraId() const
{
    return camera_id_;
}

status_t CameraDeviceSessionHwlImpl::GetCameraCharacteristics(
    std::unique_ptr<HalCameraMetadata> *characteristics) const
{
    if (characteristics == nullptr) {
        return BAD_VALUE;
    }

    (*characteristics) = HalCameraMetadata::Clone(m_meta->GetStaticMeta());
    if (*characteristics == nullptr) {
        ALOGE("%s metadata clone failed", __func__);
        return NO_MEMORY;
    }

    return OK;
}

status_t CameraDeviceSessionHwlImpl::ConstructDefaultRequestSettings(
    RequestTemplate type, std::unique_ptr<HalCameraMetadata> *default_settings)
{
    Mutex::Autolock _l(mLock);

    return m_meta->getRequestSettings(type, default_settings);
}

}  // namespace android
