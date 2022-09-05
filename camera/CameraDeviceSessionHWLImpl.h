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

#ifndef CAMERA_DEVICE_SESSION_HWL_IMPL_H
#define CAMERA_DEVICE_SESSION_HWL_IMPL_H

#include <utils/Thread.h>
#include <utils/Condition.h>
#include <utils/Mutex.h>

#include <camera_device_session_hwl.h>
#include <set>
#include <map>
#include <list>
#include "CameraDeviceHWLImpl.h"
#include "UvcStream.h"
#include "CameraMetadata.h"
#include "JpegBuilder.h"
#include "MemoryManager.h"
#include "CameraConfigurationParser.h"

#include <hardware/gralloc1.h>

using namespace fsl;

namespace android {

using google_camera_hal::CameraDeviceHwl;
using google_camera_hal::CameraDeviceSessionHwl;
using google_camera_hal::HalStream;
using google_camera_hal::HwlOfflinePipelineRole;
using google_camera_hal::HwlPipelineCallback;
using google_camera_hal::HwlPipelineRequest;
using google_camera_hal::HwlPipelineResult;
using google_camera_hal::HwlSessionCallback;
using google_camera_hal::IMulticamCoordinatorHwl;
using google_camera_hal::StreamConfiguration;
using google_camera_hal::RequestTemplate;
using google_camera_hal::SessionDataKey;
using google_camera_hal::Stream;
using google_camera_hal::StreamConfiguration;
using google_camera_hal::MessageType;
using google_camera_hal::NotifyMessage;
using google_camera_hal::StreamBuffer;
using google_camera_hal::ErrorCode;

// 3aState
struct autoState {
    uint8_t aeMode;
    uint8_t afMode;
    uint8_t awbMode;
    uint8_t aeState;
    uint8_t afState;
    uint8_t awbState;
    int32_t afTriggerId;
    int32_t aeTriggerId;
};

typedef struct tag_pipeline_info {
    uint32_t pipeline_id;
    uint32_t physical_camera_id;
    HwlPipelineCallback pipeline_callback;
   // StreamConfiguration request_config;
    std::vector<Stream>* streams;
    std::vector<HalStream>* hal_streams;
} PipelineInfo;

typedef struct tag_fence_fd_info {
    int acquire_fence_fd;
    int release_fence_fd;
} FenceFdInfo;

typedef struct tag_request {
    HwlPipelineRequest hwlReq;
    std::vector<FenceFdInfo> outBufferFences;
    std::vector<uint32_t> camera_ids;

    // In SubmitRequests(), FrameRequest is a vector, although till now the vector size is 1.
    // In imgProc::HandleImage(), FrameRequest is proced one by one.
    // So need free the vector when last FrameRequest is processed.
    int idx;
    int num;
    void *vector; // save "std::vector<FrameRequest> *frame_request"
} FrameRequest;

// Implementation of CameraDeviceSessionHwl interface
class CameraDeviceSessionHwlImpl : public CameraDeviceSessionHwl
{
public:
    static std::unique_ptr<CameraDeviceSessionHwlImpl> Create(
        uint32_t camera_id, std::unique_ptr<HalCameraMetadata> static_meta, CameraDeviceHwlImpl *pDev, PhysicalMetaMapPtr physical_devices);
    virtual ~CameraDeviceSessionHwlImpl();

    // Override functions in CameraDeviceSessionHwl
    status_t ConstructDefaultRequestSettings(
        RequestTemplate type,
        std::unique_ptr<HalCameraMetadata>* default_settings) override;

    status_t PrepareConfigureStreams(
        const StreamConfiguration& /*request_config*/) override
    {
        return OK;
    }  // Noop for now

    status_t ConfigurePipeline(uint32_t physical_camera_id,
                                HwlPipelineCallback hwl_pipeline_callback,
                                const StreamConfiguration& request_config,
                                const StreamConfiguration& overall_config,
                                uint32_t* pipeline_id) override;

    status_t BuildPipelines() override;

    status_t PreparePipeline(uint32_t /*pipeline_id*/,
                                uint32_t /*frame_number*/) override
    {
        return OK;
    }  // Noop for now

    status_t GetRequiredIntputStreams(const StreamConfiguration& /*overall_config*/,
                                        HwlOfflinePipelineRole /*pipeline_role*/,
                                        std::vector<Stream>* /*streams*/) override
    {
        return INVALID_OPERATION;
    }

    status_t GetConfiguredHalStream(
        uint32_t pipeline_id, std::vector<HalStream>* hal_streams) const override;

    void DestroyPipelines() override;

    status_t SubmitRequests(
        uint32_t frame_number,
        std::vector<HwlPipelineRequest>& requests) override;

    status_t Flush() override;

    uint32_t GetCameraId() const override;

    std::vector<uint32_t> GetPhysicalCameraIds() const override;

    status_t GetCameraCharacteristics(
        std::unique_ptr<HalCameraMetadata>* characteristics) const override;

    status_t GetPhysicalCameraCharacteristics(
        uint32_t physical_camera_id,
        std::unique_ptr<HalCameraMetadata>* characteristics) const override;

    status_t SetSessionData(SessionDataKey /*key*/
                            ,
                            void* /*value*/) override
    {
        return OK;
    }  // Noop for now

    status_t GetSessionData(SessionDataKey /*key*/,
                            void** /*value*/) const override

    {
        return OK;
    }  // Noop for now

    void SetSessionCallback(
        const HwlSessionCallback& /*hwl_session_callback*/) override
    {
    }

    status_t FilterResultMetadata(HalCameraMetadata* /*metadata*/) const override
    {
        return OK;
    }  // Noop for now

    std::unique_ptr<IMulticamCoordinatorHwl> CreateMulticamCoordinatorHwl()
        override
    {
        return nullptr;
    }

    status_t IsReconfigurationRequired(
        const HalCameraMetadata* /*old_session*/,
        const HalCameraMetadata* /*new_session*/,
        bool* reconfiguration_required) const override
    {
        if (reconfiguration_required == nullptr) {
            return BAD_VALUE;
        }
        *reconfiguration_required = true;
        return OK;
    }

    std::unique_ptr<google_camera_hal::ZoomRatioMapperHwl> GetZoomRatioMapperHwl()
        override
    {
        return nullptr;
    }

    // End override functions in CameraDeviceSessionHwl

private:
    status_t Initialize(uint32_t camera_id, std::unique_ptr<HalCameraMetadata> static_meta, CameraDeviceHwlImpl *pDev);

    CameraDeviceSessionHwlImpl(PhysicalMetaMapPtr physical_devices);

    int HandleRequest();
    status_t HandleMetaLocked(std::unique_ptr<HalCameraMetadata>& resultMeta, uint64_t timestamp);

    status_t ProcessCapbuf2MultiOutbuf(ImxStreamBuffer *srcBuf, std::vector<StreamBuffer> &output_buffers, std::vector<FenceFdInfo> &outFences, CameraMetadata& requestMeta);
    status_t ProcessCapbuf2Outbuf(ImxStreamBuffer *srcBuf, StreamBuffer &output_buffers, FenceFdInfo &outFences, CameraMetadata &requestMeta);

    int32_t processJpegBuffer(ImxStreamBuffer *srcBuf, ImxStreamBuffer *dstBuf, CameraMetadata *meta);
    int32_t processFrameBuffer(ImxStreamBuffer *srcBuf, ImxStreamBuffer *dstBuf, CameraMetadata *meta);

    ImxStreamBuffer* CreateImxStreamBufferFromStreamBuffer(StreamBuffer *buf, Stream *stream);
    void ReleaseImxStreamBuffer(ImxStreamBuffer *imxBuf);

    Stream* GetStreamFromStreamBuffer(StreamBuffer *buf);

    int CleanRequestsLocked();
    status_t PickConfigStream(uint32_t pipeline_id, uint8_t intent);
    int HandleIntent(HwlPipelineRequest *hwReq);

    int HandleImage();
    status_t CapAndFeed(uint32_t frame, FrameRequest *frameRequest);
    void DumpRequest();

private:
    class WorkThread : public Thread
    {
    public:
        WorkThread(CameraDeviceSessionHwlImpl *pSession)
            : Thread(false), mSession(pSession)
            { }

        virtual void onFirstRef() {
            run("WorkThread", PRIORITY_URGENT_DISPLAY);
        }

        virtual status_t readyToRun() {
            return 0;
        }

        virtual bool threadLoop() {
            int ret = mSession->HandleRequest();
            if (ret != OK) {
                ALOGI("%s exit...", __func__);
                return false;
            }
            return true;
        }

    private:
       CameraDeviceSessionHwlImpl *mSession;
    };

    typedef struct tag_ImageFeed {
        uint32_t frame;
        ImxStreamBuffer *v4l2Buffer;
        FrameRequest *frameRequest;
        std::vector<ImxStreamBuffer *> v4l2BufferList; // for logical camera
    } ImageFeed;

    class ImgProcThread : public Thread
    {
    public:
        ImgProcThread(CameraDeviceSessionHwlImpl *pSession)
            : Thread(false), mSession(pSession)
            { }

        virtual void onFirstRef() {
            run("ImgProcThread", PRIORITY_URGENT_DISPLAY);
        }

        virtual status_t readyToRun() {
            return 0;
        }

        virtual bool threadLoop() {
            int ret = mSession->HandleImage();
            if (ret != OK) {
                ALOGI("%s exit...", __func__);
                return false;
            }
            return true;
        }

        int feed(ImageFeed *imgFeed);
        void releaseImgFeed(ImageFeed *imgFeed);
        void drainImages(uint32_t waitItvlUs);
        void DumpImage();

    public:
        CameraDeviceSessionHwlImpl *mSession;

    public:
        Mutex mImageListLock;
        Condition mImageListCond;
        std::list<ImageFeed *> mImageList;
        uint64_t mLatestImageIdx;
        uint64_t mProcdImageIdx;
        uint32_t mLatestFrame;
        uint32_t mProcdFrame;
    };

public:
    CameraSensorMetadata* getSensorData() { return &mSensorData; }
    char* getDevPath(int i) { return (*mDevPath[i]); }
    int getCapsMode(uint8_t sceneMode);
    int32_t getRawV4l2Format() { return m_raw_v4l2_format; }
    uint32_t cameraId() { return camera_id_; }
    sp<ImgProcThread>& getImgProcThread() { return mImgProcThread; }

private:
    Mutex mLock;
    Condition mCondition;

private:
    // Protects the API entry points
    uint32_t camera_id_ = 0;
    uint32_t pipeline_id_ = 0;

    bool pipelines_built_ = false;
    CameraMetadata *m_meta;
    std::map<uint32_t, PipelineInfo*> map_pipeline_info;
    std::map<uint32_t, std::vector<FrameRequest>*> map_frame_request;

    std::vector<uint32_t> camera_ids;

    autoState m3aState;
    fsl::MemoryManager* pMemManager;
    std::vector<VideoStream*> pVideoStreams;

    sp<WorkThread> mWorkThread;
    sp<ImgProcThread> mImgProcThread;
    sp<JpegBuilder> mJpegBuilder;

    PhysicalMetaMapPtr physical_meta_map_;

    std::unique_ptr<HalCameraMetadata> static_metadata_;

    std::vector<std::shared_ptr<char*>> mDevPath;

    bool is_logical_device_ = false;
    bool is_logical_request_ = false;

    // Maps particular focal length to physical device id
    std::unordered_map<float, uint32_t> physical_focal_length_map_;
    float current_focal_length_ = 0.f;


    int previewIdx;
    int stillcapIdx;
    int recordIdx;
    int callbackIdx;

    std::unique_ptr<HalCameraMetadata> mSettings;

    CscHw mCamBlitCopyType;
    CscHw mCamBlitCscType;
    char mJpegHw[JPEG_HW_NAME_LEN] = { 0 };
    int mUseCpuEncoder;
    CameraSensorMetadata mSensorData;

    int mPreviewResolutions[MAX_RESOLUTION_SIZE];
    int mPreviewResolutionCount;
    int mPictureResolutions[MAX_RESOLUTION_SIZE];
    int mPictureResolutionCount;

    int mMaxWidth = 0;
    int mMaxHeight = 0;
    struct viv_caps_supports caps_supports;
    int32_t m_raw_v4l2_format;

    bool mDebug;
    uint64_t mPreHandleImageTime;
    uint64_t mPreCapAndFeedTime;
    uint64_t mPreSubmitRequestTime;
};

}  // namespace android

#endif  // CAMERA_DEVICE_SESSION_HWL_IMPL_H
