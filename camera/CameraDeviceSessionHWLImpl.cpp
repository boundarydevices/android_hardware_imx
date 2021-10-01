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
//#define LOG_NDEBUG 0
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
#include <sync/sync.h>

//using namespace fsl;
using namespace cameraconfigparser;
namespace android {

// ImportFence and closeFence are refed from hardware/interfaces/camera/common/1.0/default/HandleImporter.cpp.
// If use functions from HandleImporter.cpp, will lead to add a series of libs.
static int importFence(const native_handle_t* handle) {
    if (handle == nullptr || handle->numFds == 0)
        return -1;

    if (handle->numFds != 1) {
        ALOGE("invalid fence handle with %d file descriptors", handle->numFds);
        return -1;
    }

    int fd = dup(handle->data[0]);
    if (fd < 0)
        ALOGE("failed to dup fence fd %d, %s", handle->data[0], strerror(errno));

    return fd;
}

static void closeFence(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

std::unique_ptr<CameraDeviceSessionHwlImpl>
CameraDeviceSessionHwlImpl::Create(
    uint32_t camera_id, std::unique_ptr<HalCameraMetadata> pMeta, CameraDeviceHwlImpl *pDev, PhysicalMetaMapPtr physical_devices)
{
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
        ALOGE("%s: session->Initialize  failed: %s(%d)",
                __func__, strerror(-res), res);
        return nullptr;
    }

    return session;
}

status_t CameraDeviceSessionHwlImpl::Initialize(
    uint32_t camera_id, std::unique_ptr<HalCameraMetadata> pMeta, CameraDeviceHwlImpl *pDev)
{
    int ret;
    camera_id_ = camera_id;

    static_metadata_ = std::move(pMeta);

    if (pDev == NULL)
        return BAD_VALUE;

    mCamDev = pDev;
    m_meta = pDev->m_meta->Clone();

    ALOGI("Initialize, meta %p, entry count %zu", static_metadata_.get(), static_metadata_->GetEntryCount());

    mDevPath = pDev->mDevPath;
    CameraSensorMetadata *cam_metadata = &(pDev->mSensorData);
    pVideoStreams.resize(mDevPath.size());
    for (int i = 0; i < (int)mDevPath.size(); ++i) {
        ALOGI("%s: create video stream for camera %s, buffer type %d, path %s",
                __func__,
                cam_metadata->camera_name,
                cam_metadata->buffer_type,
                *mDevPath[i]);

        if (strstr(cam_metadata->camera_name, UVC_NAME)) {
            pVideoStreams[i] = new UvcStream(*mDevPath[i], this);
        } else if(strstr(cam_metadata->camera_name, ISP_SENSOR_NAME)) {
            pVideoStreams[i] = new ISPCameraMMAPStream(this);
            ((ISPCameraMMAPStream *)pVideoStreams[i])->createISPWrapper(*mDevPath[i], &mSensorData);
        } else if (cam_metadata->buffer_type == CameraSensorMetadata::kMmap) {
            pVideoStreams[i] = new MMAPStream(this);
        } else if (cam_metadata->buffer_type == CameraSensorMetadata::kDma) {
            pVideoStreams[i] = new DMAStream((bool)cam_metadata->mplane, this);
        } else {
            ALOGE("%s: unsupported camera %s, or unsupported buffer type %d", __func__, cam_metadata->camera_name, cam_metadata->buffer_type);
            return BAD_VALUE;
        }

        ALOGI("%s: VideoStream[%d] %p created, device path %s", __func__, i, pVideoStreams[i], *mDevPath[i]);

        if (pVideoStreams[i] == NULL)
            return BAD_VALUE;

        ret = pVideoStreams[i]->openDev(*mDevPath[i]);
        if (ret) {
            ALOGE("pVideoStreams[%d]->openDev failed, ret %d",i, ret);
            return BAD_VALUE;
        }
    }

    if ((physical_device_map_.get() != nullptr) && (!physical_device_map_->empty())) {
        is_logical_device_ = true;
        // If possible map the available focal lengths to individual physical devices
        camera_metadata_ro_entry_t logical_entry, physical_entry;
        ret = static_metadata_->Get(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS, &logical_entry);
        if ((ret == OK) && (logical_entry.count > 0)) {
            for (size_t i = 0; i < logical_entry.count; i++) {
                for (const auto &it : *physical_device_map_) {
                    ret = it.second->Get(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS, &physical_entry);
                    if ((ret == OK) && (physical_entry.count > 0)) {
                        if (logical_entry.data.f[i] == physical_entry.data.f[0]) {
                            physical_focal_length_map_[physical_entry.data.f[0]] = it.first;
                            ALOGI("%s: current_focal_length_ camera id: %d====\n", __FUNCTION__, it.first);
                            break;
                        }
                    }
                }
            }
        }
        current_focal_length_ = logical_entry.data.f[0];
        ALOGI("%s: current_focal_length_ set: %5.2f\n", __FUNCTION__, logical_entry.data.f[0]);
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
    mUseCpuEncoder = pDev->mUseCpuEncoder;
    mSensorData = pDev->mSensorData;

    mPreviewResolutionCount = pDev->mPreviewResolutionCount;
    memcpy(mPreviewResolutions, pDev->mPreviewResolutions, MAX_RESOLUTION_SIZE*sizeof(int));
    mPictureResolutionCount = pDev->mPictureResolutionCount;
    memcpy(mPictureResolutions, pDev->mPictureResolutions, MAX_RESOLUTION_SIZE*sizeof(int));

    mMaxWidth = pDev->mMaxWidth;
    mMaxHeight = pDev->mMaxHeight;
    caps_supports = pDev->caps_supports;
    m_raw_v4l2_format = pDev->m_raw_v4l2_format;

    return OK;
}

CameraDeviceSessionHwlImpl::CameraDeviceSessionHwlImpl(PhysicalMetaMapPtr physical_devices)
{
    ALOGI("%s: this %p", __func__, this);

    memset(&m3aState, 0, sizeof(m3aState));
    memset(&caps_supports, 0, sizeof(caps_supports));

    pMemManager = NULL;
    m_meta = NULL;
    mSettings = NULL;

    physical_device_map_ = std::move(physical_devices);
}

CameraDeviceSessionHwlImpl::~CameraDeviceSessionHwlImpl()
{
    ALOGI("%s: this %p, %p, %p", __func__, this, mWorkThread.get(), &mWorkThread);

    if(mWorkThread != NULL) {
        mWorkThread->requestExitAndWait();
        ALOGI("%s, mWorkThread exited", __func__);
    }

    int stream_size = pVideoStreams.size();
    for (int i = 0; i < stream_size; ++i) {
        if (pVideoStreams[i]) {
            pVideoStreams[i]->Stop();
            pVideoStreams[i]->closeDev();
            delete pVideoStreams[i];
            pVideoStreams[i] = NULL;
        }
    }

    if (mJpegBuilder != NULL)
        mJpegBuilder.clear();

    if(mSettings != NULL)
        mSettings.reset();
}

int CameraDeviceSessionHwlImpl::HandleIntent(HwlPipelineRequest *hwReq)
{
    camera_metadata_ro_entry entry;
    int ret = 0;
    int retSceneMode = 0;
    int retIntent = 0;
    uint8_t sceneMode = pVideoStreams[0]->mSceneMode;
    uint8_t captureIntent = pVideoStreams[0]->mCaptureIntent;

    if ((hwReq == NULL) || (hwReq->settings.get() == NULL))
        return 0;

    retSceneMode = hwReq->settings->Get(ANDROID_CONTROL_SCENE_MODE, &entry);
    if (retSceneMode == 0)
        sceneMode = entry.data.u8[0];

    retIntent = hwReq->settings->Get(ANDROID_CONTROL_CAPTURE_INTENT, &entry);
    if (retIntent == 0)
        captureIntent = entry.data.u8[0];

    // Neither scene mode, nor capture intent in the meta, no need to handle, return.
    if (retSceneMode && retIntent)
        return 0;

    int configIdx = -1;
    uint32_t pipeline_id = hwReq->pipeline_id;
    configIdx = PickConfigStream(pipeline_id, captureIntent);
    if ((configIdx < 0) && retSceneMode)
        return 0;

    uint32_t fps = 30;
    ret = hwReq->settings->Get(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, &entry);
    if ((ret == 0) && (entry.count > 1)) {
        ALOGI("%s: request fps range[%d, %d]", __func__, entry.data.i32[0], entry.data.i32[1]);
        if (entry.data.i32[0] <= 15 && entry.data.i32[1] <= 15)
            fps = 15;
        else if (strstr(mSensorData.camera_name, ISP_SENSOR_NAME))
            fps = entry.data.i32[0];
    }

    // In HDR mode, max fps is 30
    if ((sceneMode == ANDROID_CONTROL_SCENE_MODE_HDR) && (fps > 30))
        fps = 30;

    PipelineInfo *pipeline_info = map_pipeline_info[pipeline_id];

    //TODO need to refine for logical's configIdx ?
    if(is_logical_request_) {
        auto stat = hwReq->settings->Get(ANDROID_LENS_FOCAL_LENGTH, &entry);
        if ((stat == OK) && (entry.count == 1)) {
            current_focal_length_ = entry.data.f[0];
            ALOGI("%s: requests' focal length set: %5.2f",__FUNCTION__, entry.data.f[0]);
        } else {
            ALOGW("%s: Focal length absent from request!", __FUNCTION__);
        }

        ret = 0;
        for (size_t index = 0; index < pVideoStreams.size(); index++) {
            pVideoStreams[index]->SetBufferNumber(pipeline_info->hal_streams->at(0).max_buffers + 1);
            ret += pVideoStreams[index]->ConfigAndStart(HAL_PIXEL_FORMAT_YCbCr_422_I,
                                        pipeline_info->streams->at(0).width,
                                        pipeline_info->streams->at(0).height,
                                        fps, captureIntent, sceneMode);
            if (ret)
                ALOGE("%s: pVideoStreams[%zu]->ConfigAndStart failed, ret %d", __func__, index, ret);
        }
    } else {
        pVideoStreams[0]->SetBufferNumber(pipeline_info->hal_streams->at(configIdx).max_buffers + 1);

        uint32_t format = HAL_PIXEL_FORMAT_YCbCr_422_I;
        if (strcmp(mSensorData.v4l2_format, "nv12") == 0)
            format = HAL_PIXEL_FORMAT_YCbCr_420_SP;

        if(pipeline_info->hal_streams->at(configIdx).override_format == HAL_PIXEL_FORMAT_RAW16) {
            format = HAL_PIXEL_FORMAT_RAW16;
        }

        // v4l2 hard code to use yuv422i. If in future other foramts are used, need refine code, maybe configed in json
        ret = pVideoStreams[0]->ConfigAndStart(format,
                                            pipeline_info->streams->at(configIdx).width,
                                            pipeline_info->streams->at(configIdx).height,
                                            fps, captureIntent, sceneMode);
        if (ret)
            ALOGE("%s: pVideoStreams[0]->ConfigAndStart failed, ret %d", __func__, ret);
    }

    return ret;
}

#define WAIT_TIME_OUT 100000000LL  // unit ns, wait 100ms
int CameraDeviceSessionHwlImpl::HandleRequest()
{
    Mutex::Autolock _l(mLock);

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
        std::vector<FrameRequest> *request = it->second;
        uint32_t reqNum = request->size();

        ALOGV("%s: frame %d, request num %d", __func__, frame, reqNum);

        for (uint32_t i = 0; i < reqNum; i++) {
            HwlPipelineRequest *hwReq = &(request->at(i).hwlReq);
            std::vector<FenceFdInfo> outFences  = request->at(i).outBufferFences;
            uint32_t pipeline_id = hwReq->pipeline_id;
            ALOGV("%s: request: pipeline_id %d, output buffer num %d, meta %p",
                    __func__,
                    (int)pipeline_id,
                    (int)hwReq->output_buffers.size(),
                    hwReq->settings.get());

            PipelineInfo *pInfo = map_pipeline_info[pipeline_id];
            if(pInfo == NULL) {
                ALOGW("%s: Unexpected, pipeline %d is invalid",__func__, pipeline_id);
                continue;
            }

            HandleIntent(hwReq);

            uint64_t timestamp = 0;
            // notify shutter
            if (mUseCpuEncoder)
                timestamp = systemTime(SYSTEM_TIME_MONOTONIC);
            else
                timestamp = systemTime(SYSTEM_TIME_BOOTTIME);

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

            for (int stream_id = 0; stream_id < (int)pVideoStreams.size(); ++stream_id) {
                pVideoStreams[stream_id]->ISPProcess(mSettings.get());
            }

            auto result = std::make_unique<HwlPipelineResult>();

            if (mSettings != NULL)
                result->result_metadata = HalCameraMetadata::Clone(mSettings.get());
            else
                result->result_metadata = HalCameraMetadata::Create(1, 10);

            // process frame
            CameraMetadata requestMeta(result->result_metadata.get());
            HandleFrameLocked(hwReq->output_buffers, outFences, requestMeta);

            // return result
            result->camera_id = camera_id_;
            result->pipeline_id = pipeline_id;
            result->frame_number = frame;
            result->partial_result = 1;

            if(is_logical_request_) {
                std::unique_ptr<std::set<uint32_t>> physical_camera_output_ids = std::make_unique<std::set<uint32_t>>();

                int output_buffers_size = (int)hwReq->output_buffers.size();
                for (int output_buf_id = 0; output_buf_id < output_buffers_size; output_buf_id++) {
                    int phyid_cam_id = request->at(i).camera_ids[output_buf_id];
                    if (phyid_cam_id != (int)camera_id_) {
                        physical_camera_output_ids->emplace(phyid_cam_id);
                    }
                }

                if ((physical_camera_output_ids.get() != nullptr) &&
                    (!physical_camera_output_ids->empty())) {
                    result->physical_camera_results.reserve(physical_camera_output_ids->size());

                    for(int id = 0; id < (int)hwReq->output_buffers.size(); id++) {
                        auto phy_result = std::make_unique<HwlPipelineResult>();
                        // return physical buffer
                        phy_result->camera_id = camera_ids[id];
                        phy_result->pipeline_id = pipeline_id;
                        phy_result->frame_number = frame;
                        // This value must be set to 0 when a capture result contains buffers only and no metadata.
                        phy_result->partial_result = 0;
                        phy_result->output_buffers.push_back(hwReq->output_buffers[id]);

                        // call back to process physical buffer
                        if (pInfo->pipeline_callback.process_pipeline_result)
                            pInfo->pipeline_callback.process_pipeline_result(std::move(phy_result));
                    }

                    for (const auto &it : *physical_camera_output_ids) {
                        std::unique_ptr<HalCameraMetadata> physical_metadata_ = nullptr;
                        if (mSettings != NULL)
                            physical_metadata_ = HalCameraMetadata::Clone(mSettings.get());
                        else
                            physical_metadata_ = HalCameraMetadata::Create(1, 10);

                        // Sensor timestamp for all physical devices must be the same.
                        HandleMetaLocked(physical_metadata_, timestamp);

                        result->physical_camera_results[it] = std::move(physical_metadata_);
                    }
                }
            } else {
                result->output_buffers.assign(hwReq->output_buffers.begin(), hwReq->output_buffers.end());
                result->input_buffers.reserve(0);
                result->physical_camera_results.reserve(0);
            }

            ALOGV("result->regsult_metadata %p, entry count %d",
                    result->result_metadata.get(),
                    (int)result->result_metadata->GetEntryCount());

            if(is_logical_device_) {
                auto physical_device_id = std::to_string(physical_focal_length_map_[current_focal_length_]);
                ALOGV("%s: ANDROID_LOGICAL_MULTI_CAMERA_ACTIVE_PHYSICAL_ID is %s",__func__, physical_device_id.c_str());

                std::vector<uint8_t> ret;
                ret.reserve(physical_device_id.size() + 1);
                ret.insert(ret.end(), physical_device_id.begin(), physical_device_id.end());
                ret.push_back('\0');
                result->result_metadata->Set(ANDROID_LOGICAL_MULTI_CAMERA_ACTIVE_PHYSICAL_ID,
                                        ret.data(), ret.size());
            }

            HandleMetaLocked(result->result_metadata, timestamp);

            // call back to process result
            if (pInfo->pipeline_callback.process_pipeline_result)
                pInfo->pipeline_callback.process_pipeline_result(std::move(result));
        }
    }

    CleanRequestsLocked();

    return OK;
}

status_t CameraDeviceSessionHwlImpl::HandleFrameLocked(std::vector<StreamBuffer> &output_buffers, std::vector<FenceFdInfo> &outFences, CameraMetadata& requestMeta)
{
    int ret = 0;

    if(is_logical_request_) {
        std::vector<ImxStreamBuffer *> pImxStreamBuffers;
        pImxStreamBuffers.resize(pVideoStreams.size());

        for (size_t index = 0; index < output_buffers.size(); index++) {
            ImxStreamBuffer *pImxStreamBuffer;
            int video_num = index;
            if (video_num >= (int)pVideoStreams.size()) {
                video_num = video_num - pVideoStreams.size();
            }
            pImxStreamBuffer = pVideoStreams[video_num]->onFrameAcquireLocked();
            if (pImxStreamBuffer == NULL) {
                ALOGW("onFrameAcquireLocked failed");
                usleep(5000);
                return 0;
            }

            ret += ProcessCapturedBuffer2(pImxStreamBuffer, output_buffers[index], outFences[index], requestMeta);

            pVideoStreams[video_num]->onFrameReturnLocked(*pImxStreamBuffer);
        }
    } else {
        ImxStreamBuffer *pImxStreamBuffer;

        pImxStreamBuffer = pVideoStreams[0]->onFrameAcquireLocked();
        if (pImxStreamBuffer == NULL) {
            ALOGW("onFrameAcquireLocked failed");
            usleep(5000);
            return 0;
        }

        if (strstr(mSensorData.camera_name, ISP_SENSOR_NAME)) {
            camera_metadata_ro_entry entry;
            ret = requestMeta.Get(ANDROID_CONTROL_ZOOM_RATIO, &entry);
            if (ret == 0) {
                pImxStreamBuffer->mStream->mZoomRatio = entry.data.f[0];
            }
        }

        ret = ProcessCapturedBuffer(pImxStreamBuffer, output_buffers, outFences, requestMeta);

        pVideoStreams[0]->onFrameReturnLocked(*pImxStreamBuffer);
    }
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

    ALOGV("%s, buffer: virt %p, phy 0x%lx, size %zu, format 0x%x, acquire_fence %p, release_fence %p, stream: res %dx%d, format 0x%x, size %d",
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

    bool bPreview = false;
    if ( (stream->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) && ((stream->usage & GRALLOC_USAGE_HW_VIDEO_ENCODER) == 0) )
        bPreview = true;

    imxBuf->mStream = new ImxStream(stream->width, stream->height, handle->format, stream->usage, stream->id, bPreview);

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

status_t CameraDeviceSessionHwlImpl::ProcessCapturedBuffer2(ImxStreamBuffer *srcBuf, StreamBuffer &output_buffers, FenceFdInfo &outFences, CameraMetadata &requestMeta)
{
    int ret = 0;
    if (srcBuf == NULL)
        return BAD_VALUE;

    StreamBuffer *it = &output_buffers;

    // If fence is valid, wait.  Ref the usage in EmulatedRequestProcessor.cpp.
    int acquire_fence_fd = outFences.acquire_fence_fd;
    if (acquire_fence_fd > -1) {
        ALOGV("%s, before sync_wait fence fd %d", __func__, acquire_fence_fd);
        ret = sync_wait(acquire_fence_fd, CAMERA_SYNC_TIMEOUT);
        ALOGV("%s, after sync_wait fence fd %d", __func__, acquire_fence_fd);
        closeFence(acquire_fence_fd);
        if (ret != OK) {
            ALOGW("%s: Timeout waiting on acquire fence %d, on stream %d, buffer %lu", __func__, acquire_fence_fd, it->stream_id, it->buffer_id);
        }
    }

    Stream *pStream = GetStreamFromStreamBuffer(it);
    if (pStream == NULL) {
        ALOGE("%s, dst buf belong to stream %d, but the stream is not configured", __func__, it->stream_id);
        return BAD_VALUE;
    }

    ImxStreamBuffer *dstBuf = CreateImxStreamBufferFromStreamBuffer(it, pStream);
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
    return 0;
}

status_t CameraDeviceSessionHwlImpl::ProcessCapturedBuffer(ImxStreamBuffer *srcBuf, std::vector<StreamBuffer> &output_buffers, std::vector<FenceFdInfo> &outFences, CameraMetadata &requestMeta)
{
    int ret = 0;

    if (srcBuf == NULL)
        return BAD_VALUE;
    int outBufSize = output_buffers.size();
    int outFencesSize = outFences.size();

    if(outBufSize != outFencesSize) {
        ALOGE("%s: outBufSize(%d) != outFencesSize(%d)", __func__, outBufSize, outFencesSize);
        return BAD_VALUE;
    }

    for (int i = 0; i < outBufSize; i++) {
        StreamBuffer *it = &output_buffers[i];

        if (it == NULL) {
            ALOGE("%s: output_buffers[%d] is invalid", __func__, i);
            return BAD_VALUE;
        }

        // If fence is valid, wait.  Ref the usage in EmulatedRequestProcessor.cpp.
        int acquire_fence_fd = outFences[i].acquire_fence_fd;
        if (acquire_fence_fd > -1) {
            ALOGV("%s, before sync_wait fence fd %d", __func__, acquire_fence_fd);
            ret = sync_wait(acquire_fence_fd, CAMERA_SYNC_TIMEOUT);
            ALOGV("%s, after sync_wait fence fd %d", __func__, acquire_fence_fd);
            closeFence(acquire_fence_fd);
            if (ret != OK) {
                ALOGW("%s: Timeout waiting on acquire fence %d, on stream %d, buffer %lu", __func__, acquire_fence_fd, it->stream_id, it->buffer_id);
                continue;
            }
        }
        ALOGV("%s: fence %d, on stream %d, buffer %lu", __func__, acquire_fence_fd, it->stream_id, it->buffer_id);

        Stream *pStream = GetStreamFromStreamBuffer(it);
        if (pStream == NULL) {
            ALOGE("%s, dst buf belong to stream %d, but the stream is not configured", __func__, it->stream_id);
            return BAD_VALUE;
        }

        ImxStreamBuffer *dstBuf = CreateImxStreamBufferFromStreamBuffer(it, pStream);
        if (dstBuf == NULL)
            return BAD_VALUE;

        uint64_t t1 = systemTime();
        if (dstBuf->mStream->format() == HAL_PIXEL_FORMAT_BLOB) {
            mJpegBuilder->reset();
            mJpegBuilder->setMetadata(&requestMeta);

            ret = processJpegBuffer(srcBuf, dstBuf, &requestMeta);
        } else
            ret = processFrameBuffer(srcBuf, dstBuf, &requestMeta);
        uint64_t t2 = systemTime();

        char value[PROPERTY_VALUE_MAX];
        property_get("vendor.rw.camera.test", value, "");
        if (strcmp(value, "timestat") == 0) {
            ALOGI("ProcessCapturedBuffer, process buf %d, use %lu ms, src: size %dx%d, format 0x%x, dst: size %dx%d, format 0x%x",
                i, (t2-t1)/1000000,
                srcBuf->mStream->width(), srcBuf->mStream->height(), srcBuf->mStream->format(),
                dstBuf->mStream->width(), dstBuf->mStream->height(), dstBuf->mStream->format());
        }

        DumpStream(srcBuf->mVirtAddr, srcBuf->mFormatSize, dstBuf->mVirtAddr, dstBuf->mFormatSize, dstBuf->mStream->id());

        ReleaseImxStreamBuffer(dstBuf);
    }

    return ret;
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
        srcStream->format() == dstStream->format() &&
        srcStream->mZoomRatio <= 1.0)
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

    // Handle zoom in
    if (srcStream->mZoomRatio > 1.0) {
        resizeBuf.mFormatSize = srcBuf->mFormatSize;
        resizeBuf.mSize = (resizeBuf.mFormatSize + PAGE_SIZE) & (~(PAGE_SIZE - 1));
        ret = AllocPhyBuffer(resizeBuf);
        if (ret) {
            ALOGE("%s:%d AllocPhyBuffer failed", __func__, __LINE__);
            return BAD_VALUE;
        }

        resizeBuf.mStream = srcBuf->mStream;
        fsl::ImageProcess *imageProcess = fsl::ImageProcess::getInstance();
        imageProcess->handleFrame(resizeBuf, *srcBuf, mCamBlitCscType);

        SwitchImxBuf(*srcBuf, resizeBuf);
    }

    mainJpeg = new JpegParams((uint8_t *)srcBuf->mVirtAddr,
                                (uint8_t *)(uintptr_t)srcBuf->mPhyAddr,
                                srcBuf->mSize,
                                srcBuf->mFd,
                                srcBuf->buffer,
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
                                srcBuf->mFd,
                                srcBuf->buffer,
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
    if (mainJpeg != NULL)
        delete mainJpeg;

    if (thumbJpeg != NULL)
        delete thumbJpeg;

    if (resizeBuf.mPhyAddr > 0) {
        SwitchImxBuf(*srcBuf, resizeBuf);
        FreePhyBuffer(resizeBuf);
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
    resultMeta->Set(ANDROID_SENSOR_TIMESTAMP, (int64_t *)&timestamp, 1);

    // auto focus control.
    ret = resultMeta->Get(ANDROID_CONTROL_AF_MODE, &entry);
    if (ret == NAME_NOT_FOUND) {
        ALOGE("%s: No AF mode entry!", __FUNCTION__);
        return BAD_VALUE;
    }
    uint8_t afMode = (entry.count > 0) ?
        entry.data.u8[0] : (uint8_t)ANDROID_CONTROL_AF_MODE_OFF;

    ret = resultMeta->Get(ANDROID_CONTROL_AF_TRIGGER, &entry);
    if (ret != NAME_NOT_FOUND) {
        // save trigger value
        uint8_t trigger = entry.data.u8[0];

        // check if a ROI has been provided
        ret = resultMeta->Get(ANDROID_CONTROL_AF_REGIONS, &entry);
        if ((ret != NAME_NOT_FOUND) && (entry.count > 3)) {
            int xavg = (entry.data.i32[0] + entry.data.i32[2]) / 2;
            int yavg = (entry.data.i32[1] + entry.data.i32[3]) / 2;
            if (xavg || yavg) {
                ALOGV("%s: AF region: x %d y %d", __FUNCTION__, xavg, yavg);
                mCamDev->setAutoFocusRegion(xavg, yavg);
            }
        }

        // get and save trigger ID
        ret = resultMeta->Get(ANDROID_CONTROL_AF_TRIGGER_ID, &entry);
        if (ret != NAME_NOT_FOUND)
            m3aState.afTriggerId = entry.data.i32[0];

        // process trigger type
        ALOGV("trigger: %d afMode %d afTriggerId %d", trigger, afMode, m3aState.afTriggerId);
        switch (trigger) {
            case ANDROID_CONTROL_AF_TRIGGER_CANCEL:
                // in case of continuous focus, cancel means to stop manual focus only
                if ((afMode == ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO) ||
                    (afMode == ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE))
                    m3aState.afState = mCamDev->doAutoFocus(afMode);
                break;
            case ANDROID_CONTROL_AF_TRIGGER_START:
                m3aState.afState = mCamDev->doAutoFocus(afMode);
                break;
            case ANDROID_CONTROL_AF_TRIGGER_IDLE:
                if ((afMode == ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO) ||
                    (afMode == ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE))
                    if (afMode != m3aState.afMode)
                        m3aState.afState = mCamDev->doAutoFocus(afMode);
                    else
                        m3aState.afState = ANDROID_CONTROL_AF_STATE_PASSIVE_SCAN;
                else
                    m3aState.afState = ANDROID_CONTROL_AF_STATE_INACTIVE;
                break;
            default:
                ALOGE("unknown trigger: %d", trigger);
                m3aState.afState = ANDROID_CONTROL_AF_STATE_INACTIVE;
        }
        // save current mode
        m3aState.afMode = afMode;
    } else {
        m3aState.afState = mCamDev->getAutoFocusStatus(afMode);
    }
    resultMeta->Set(ANDROID_CONTROL_AF_TRIGGER_ID, &m3aState.afTriggerId, 1);
    resultMeta->Set(ANDROID_CONTROL_AF_STATE, &m3aState.afState, 1);

    // auto white balance control.
    m3aState.awbState = ANDROID_CONTROL_AWB_STATE_CONVERGED;
    resultMeta->Set(ANDROID_CONTROL_AWB_STATE, &m3aState.awbState, 1);

    if (strstr(mSensorData.camera_name, ISP_SENSOR_NAME)) {

        std::unique_ptr<ISPWrapper>& ispWrapper = ((ISPCameraMMAPStream *)pVideoStreams[0])->getIspWrapper();

        int64_t exposure_time = ispWrapper->getExposureTime();
        resultMeta->Set(ANDROID_SENSOR_EXPOSURE_TIME, &exposure_time, 1);

        // Ref https://developer.android.com/reference/android/hardware/camera2/CaptureResult#SENSOR_SENSITIVITY
        ret = resultMeta->Get(ANDROID_SENSOR_SENSITIVITY, &entry);
        // Currently, ISP not support to set sensitivity. So just return the vaule in the request (resultMeta is cloned from requestMeta).
        // If there's no ANDROID_SENSOR_SENSITIVITY in request, also need set in resultMeta due to "full" level requirement.
        if(ret != OK) {
            // Ref value from GCH EmulatedCamera
            int32_t sensitivity = 1000;
            resultMeta->Set(ANDROID_SENSOR_SENSITIVITY, &sensitivity, 1);
        }

        // Ref https://developer.android.com/reference/android/hardware/camera2/CaptureRequest#CONTROL_POST_RAW_SENSITIVITY_BOOST
        // Ref value from GCH EmulatedCamera
        int32_t sensitivity_boost = 100;
        resultMeta->Set(ANDROID_CONTROL_POST_RAW_SENSITIVITY_BOOST, &sensitivity_boost, 1);

        // Ref https://developer.android.com/reference/android/hardware/camera2/CaptureResult#SENSOR_NEUTRAL_COLOR_POINT
        // Ref value from GCH EmulatedCamera
        camera_metadata_rational_t android_sensor_neutral_color_point[] = { {255, 1}, {255, 1}, {255, 1} };
        resultMeta->Set(ANDROID_SENSOR_NEUTRAL_COLOR_POINT,
                        android_sensor_neutral_color_point,
                        ARRAY_SIZE(android_sensor_neutral_color_point));

        // Ref https://developer.android.com/reference/android/hardware/camera2/CaptureResult#SENSOR_NOISE_PROFILE
        // No such parameter for basler camera, ref emulated camera.
        double noise_gain = 4.0;
        double read_noise = 9.951316;
        double noise_profile[] = {noise_gain, read_noise, noise_gain, read_noise, noise_gain, read_noise, noise_gain, read_noise};
        resultMeta->Set(ANDROID_SENSOR_NOISE_PROFILE, noise_profile, ARRAY_SIZE(noise_profile));

        // Ref https://developer.android.com/reference/android/hardware/camera2/CaptureResult#SENSOR_GREEN_SPLIT
        // No such parameter for basler camera, ref emulated camera.
        float green_split = 1.0;
        resultMeta->Set(ANDROID_SENSOR_GREEN_SPLIT, &green_split, 1);

        // Ref https://developer.android.com/reference/android/hardware/camera2/CaptureResult#SENSOR_ROLLING_SHUTTER_SKEW
        // Ref emulated camera, use min frame duration, 1/30 s.
        static const int64_t rolling_shutter_skew = 33333333; // ns
        resultMeta->Set(ANDROID_SENSOR_ROLLING_SHUTTER_SKEW, &rolling_shutter_skew, 1);

        // Ref https://developer.android.com/reference/android/hardware/camera2/CaptureResult#STATISTICS_SCENE_FLICKER
        uint8_t scene_flicker = ANDROID_STATISTICS_SCENE_FLICKER_NONE;
        resultMeta->Set(ANDROID_STATISTICS_SCENE_FLICKER, &scene_flicker, 1);

        // Ref https://developer.android.com/reference/android/hardware/camera2/CaptureRequest#STATISTICS_LENS_SHADING_MAP_MODE
        uint8_t lens_shading_map_mode = ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF;
        resultMeta->Set(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, &lens_shading_map_mode, 1);

        // Ref https://developer.android.com/reference/android/hardware/camera2/CaptureResult#LENS_STATE
        uint8_t lens_state = ANDROID_LENS_STATE_STATIONARY;
        resultMeta->Set(ANDROID_LENS_STATE, &lens_state, 1);

        // Ref https://developer.android.com/reference/android/hardware/camera2/CaptureResult#BLACK_LEVEL_LOCK
        uint8_t black_level_lock = ANDROID_BLACK_LEVEL_LOCK_ON;
        resultMeta->Set(ANDROID_BLACK_LEVEL_LOCK, &black_level_lock, 1);

        // Ref https://developer.android.com/reference/android/hardware/camera2/CaptureResult#LENS_FOCUS_RANGE
        float focus_range[] = {0.f, 0.f};
        resultMeta->Set(ANDROID_LENS_FOCUS_RANGE, focus_range, ARRAY_SIZE(focus_range));

        ret = resultMeta->Get(ANDROID_CONTROL_AE_LOCK, &entry);
        if (ret == OK) {
            uint8_t lock = entry.data.u8[0];
            if (lock == ANDROID_CONTROL_AE_LOCK_ON) {
                uint8_t status = ANDROID_CONTROL_AE_STATE_LOCKED;
                resultMeta->Set(ANDROID_CONTROL_AE_STATE, &status, 1);
            }
        }
    }

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
        ALOGE("%s Cannot configure pipelines after calling BuildPipelines()", __func__);
        return ALREADY_EXISTS;
    }

    bool bSupport = CameraDeviceHwlImpl::StreamCombJudge(request_config,
        mPreviewResolutions, mPreviewResolutionCount,
        mPictureResolutions, mPictureResolutionCount);

    if (bSupport == false) {
        ALOGI("%s: IsStreamCombinationSupported return false", __func__);
        return BAD_VALUE;
    }

    if ((physical_camera_id != camera_id_) &&
        (physical_device_map_.get() != nullptr)) {
        if (physical_device_map_->find(physical_camera_id) ==
            physical_device_map_->end())
        {
            ALOGE("%s: Camera: %d doesn't include physical device with id: %u",
                __FUNCTION__, camera_id_, physical_camera_id);
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

        uint32_t mcamera_id = stream.is_physical_camera_stream ? stream.physical_camera_id : camera_id_;
        camera_ids.push_back(mcamera_id);

        HalStream hal_stream;
        memset(&hal_stream, 0, sizeof(hal_stream));
        int usage = 0;

        switch (stream.format) {
            case HAL_PIXEL_FORMAT_RAW16:
            case HAL_PIXEL_FORMAT_BLOB:
                ALOGI("%s create capture stream", __func__);
                hal_stream.override_format = stream.format;
                hal_stream.max_buffers = NUM_CAPTURE_BUFFER;
                usage = CAMERA_GRALLOC_USAGE_JPEG;
                stillcapIdx = i;
                break;

            case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
                if (strcmp(mSensorData.v4l2_format, "nv12") == 0)
                    hal_stream.override_format = HAL_PIXEL_FORMAT_YCBCR_420_888;
                else
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
        hal_stream.is_physical_camera_stream = stream.is_physical_camera_stream;
        hal_stream.physical_camera_id = stream.physical_camera_id;

        pipeline_info->hal_streams->push_back(std::move(hal_stream));

        if (hal_stream.is_physical_camera_stream != 0) {
            is_logical_request_ = true;
        }
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

    ALOGI("%s: previewIdx %d, callbackIdx %d, stillcapIdx %d, recordIdx %d, intent %d",
        __func__, previewIdx, callbackIdx, stillcapIdx, recordIdx, intent);

    int configIdx = -1;
    if(intent == ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE)
        configIdx = stillcapIdx;


    // In this case, pick max size from callback and stillcap.
    // Or testAllOutputYUVResolutions will failed due to diff too much
    // when 320x240 enlarge to 2592x1944 by adding black margin.
    if ((strcmp(mSensorData.v4l2_format, "nv12") == 0) &&
        (stillcapIdx >= 0) && (callbackIdx >= 0) && (previewIdx < 0) && (recordIdx < 0) &&
        (intent == ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW)) {
        int stillcapWidth = pipeline_info->streams->at(stillcapIdx).width;
        int stillcapHeight = pipeline_info->streams->at(stillcapIdx).height;
        int callbackWidth = pipeline_info->streams->at(callbackIdx).width;
        int callbackHeight = pipeline_info->streams->at(callbackIdx).height;

        if ((callbackWidth > stillcapWidth) && (callbackHeight > stillcapHeight))
          configIdx = callbackIdx;
        else
          configIdx = stillcapIdx;
    }

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
        ALOGW("%s: still has %lu requests to process, wait %d ms", __func__, map_frame_request.size(), DESTROY_WAIT_MS);
        mLock.unlock();
        usleep(DESTROY_WAIT_US);
        mLock.lock();
    }

    // If still remain requests, just send out result to return buffers to framework.
    for (auto it = map_frame_request.begin(); it != map_frame_request.end(); it++) {
        uint32_t frame = it->first;
        std::vector<FrameRequest> *request = it->second;
        if(request == NULL) {
            ALOGW("%s: frame %d request is null", __func__, frame);
            continue;
        }

        uint32_t reqNum = request->size();
        ALOGI("%s, frame %d still has %d requests to process, just send back result", __func__, frame, reqNum);

        for (uint32_t i = 0; i < reqNum; i++) {
            HwlPipelineRequest *hwReq = &(request->at(i).hwlReq);
            uint32_t pipeline_id = hwReq->pipeline_id;
            PipelineInfo *pInfo = map_pipeline_info[pipeline_id];
            if(pInfo == NULL) {
                ALOGW("%s: Unexpected, pipeline %d is invalid",__func__, pipeline_id);
                continue;
            }

            auto result = std::make_unique<HwlPipelineResult>();
            result->camera_id = camera_id_;
            result->pipeline_id = hwReq->pipeline_id;
            result->frame_number = frame;
            result->result_metadata = HalCameraMetadata::Clone(hwReq->settings.get());
            result->input_buffers = hwReq->input_buffers;
            result->output_buffers = hwReq->output_buffers;
            result->partial_result = 1;
            if (pInfo->pipeline_callback.process_pipeline_result)
                pInfo->pipeline_callback.process_pipeline_result(std::move(result));
       }
    }

    CleanRequestsLocked();

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
    uint32_t frame_number, std::vector<HwlPipelineRequest> &requests)
{
    Mutex::Autolock _l(mLock);

    int size = requests.size();
    std::vector<FrameRequest> *frame_request = new std::vector<FrameRequest>(size);

    for (int i = 0; i < size; i++) {
        uint32_t pipeline_id = requests[i].pipeline_id;
        ALOGV("%s, frame_number %d, pipeline_id %d, outbuffer num %d",
                __func__,
                frame_number,
                (int)pipeline_id,
                (int)requests[i].output_buffers.size());

        frame_request->at(i).hwlReq.pipeline_id = pipeline_id;
        frame_request->at(i).hwlReq.settings = HalCameraMetadata::Clone(requests[i].settings.get());
        frame_request->at(i).hwlReq.output_buffers.reserve(requests[i].output_buffers.size());
        frame_request->at(i).hwlReq.output_buffers.assign(requests[i].output_buffers.begin(), requests[i].output_buffers.end());

        frame_request->at(i).hwlReq.input_buffers.reserve(requests[i].input_buffers.size());
        frame_request->at(i).hwlReq.input_buffers.assign(requests[i].input_buffers.begin(), requests[i].input_buffers.end());
        frame_request->at(i).hwlReq.input_buffer_metadata.reserve(requests[i].input_buffer_metadata.size());

        frame_request->at(i).camera_ids.reserve(camera_ids.size());
        frame_request->at(i).camera_ids.assign(camera_ids.begin(), camera_ids.end());

        // Record fence fd
        uint32_t outBufNum = requests[i].output_buffers.size();
        frame_request->at(i).outBufferFences.resize(outBufNum);
        for (int j = 0; j < (int)outBufNum; j++) {
            FenceFdInfo fenceInfo = {-1, -1};
            fenceInfo.acquire_fence_fd = importFence(requests[i].output_buffers[j].acquire_fence);
            ALOGV("%s, acquire_fence_fd %d", __func__, fenceInfo.acquire_fence_fd);
            frame_request->at(i).outBufferFences[j] = fenceInfo;
        }
    }

    map_frame_request[frame_number] = frame_request;
    mCondition.signal();

    return OK;
}

int CameraDeviceSessionHwlImpl::CleanRequestsLocked()
{
    if (map_frame_request.empty())
        return 0;

    for (auto it = map_frame_request.begin(); it != map_frame_request.end(); it++) {
        uint32_t frame = it->first;
        std::vector<FrameRequest> *request = it->second;
        if(request == NULL) {
            ALOGW("%s: frame %d request is null", __func__, frame);
            continue;
        }

        uint32_t reqNum = request->size();
        ALOGV("%s, map_frame_request, frame %d, reqNum %d", __func__, frame, reqNum);

        for (uint32_t i = 0; i < reqNum; i++) {
            HwlPipelineRequest *hwReq = &(request->at(i).hwlReq);

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
    //TODO need refine for multi camera??
    return OK;
}

uint32_t CameraDeviceSessionHwlImpl::GetCameraId() const
{
    return camera_id_;
}

std::vector<uint32_t> CameraDeviceSessionHwlImpl::GetPhysicalCameraIds() const {
    if ((physical_device_map_->empty())) {
        ALOGV("%s: GetPhysicalCameraIds is empty", __func__);
        return std::vector<uint32_t>{};
    }

    if ((physical_device_map_.get() == nullptr) ) {
        ALOGW("%s: GetPhysicalCameraIds is null", __func__);
        return std::vector<uint32_t>{};
    }
    std::vector<uint32_t> ret;
    ret.reserve(physical_device_map_->size());
    for (const auto& it : *physical_device_map_) {
        ret.push_back(it.first);
    }

    return ret;
}

status_t CameraDeviceSessionHwlImpl::GetCameraCharacteristics(
    std::unique_ptr<HalCameraMetadata> *characteristics) const
{
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
    uint32_t physical_camera_id, std::unique_ptr<HalCameraMetadata>* characteristics) const {
    if (characteristics == nullptr) {
        return BAD_VALUE;
    }

    if (physical_device_map_.get() == nullptr) {
        ALOGE("%s: Camera: %d doesn't have physical device support!", __FUNCTION__, camera_id_);
        return BAD_VALUE;
    }

    if (physical_device_map_->find(physical_camera_id) == physical_device_map_->end()) {
        ALOGE("%s: Camera: %d doesn't include physical device with id: %u",
            __FUNCTION__, camera_id_, physical_camera_id);
        return BAD_VALUE;
    }

    *characteristics = HalCameraMetadata::Clone((physical_device_map_->at(physical_camera_id)).get());
    return OK;
}

status_t CameraDeviceSessionHwlImpl::ConstructDefaultRequestSettings(
    RequestTemplate type, std::unique_ptr<HalCameraMetadata> *default_settings)
{
    Mutex::Autolock _l(mLock);

    return m_meta->getRequestSettings(type, default_settings);
}

int CameraDeviceSessionHwlImpl::getCapsMode(uint8_t sceneMode)
{
    bool bHdr = (sceneMode == ANDROID_CONTROL_SCENE_MODE_HDR);

    for(unsigned int i = 0; i < caps_supports.count; i++) {
        struct viv_caps_mode_info_s &mode = caps_supports.mode[i];
        if ( ((bHdr && mode.hdr_mode > 0) || (!bHdr && mode.hdr_mode == 0)) &&
             (mMaxWidth == (int)mode.bounds_width) && (mMaxHeight == (int)mode.bounds_height) ) {
            ALOGI("%s: check idx %d, find ISP mode %d for size %dx%d, bHdr %d, caps count %d",
                __func__, i, mode.index, mMaxWidth, mMaxHeight, bHdr, caps_supports.count);
            return mode.index;
        }
    }

    ALOGW("%s: can't find ISP mode for size %dx%d, bHdr %d, caps count %d, just return 0",
        __func__, mMaxWidth, mMaxHeight, bHdr, caps_supports.count);

    return 0;
}

}  // namespace android
