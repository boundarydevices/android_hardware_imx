/*
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

#ifndef _REQUEST_MANAGER_H
#define _REQUEST_MANAGER_H

#include "CameraUtil.h"
#include "StreamAdapter.h"

using namespace android;

#define REQUEST_TYPE_PREVIEW  1
#define REQUEST_TYPE_RECORD   2
#define REQUEST_TYPE_CAPTURE  4

#define PreviewRequestIdStart   10000000
#define PreviewRequestIdEnd     20000000
#define RecordingRequestIdStart 20000000
#define RecordingRequestIdEnd   30000000
#define CaptureRequestIdStart   30000000
#define CaptureRequestIdEnd     40000000

#define STREAM_ID_PREVIEW           (0)
#define STREAM_ID_RECORD            (1)
#define STREAM_ID_PRVCB             (2)
#define STREAM_ID_JPEG              (3)
#define STREAM_ID_ZSL               (4)
#define STREAM_ID_JPEG_REPROCESS    (5)
#define STREAM_ID_LAST              STREAM_ID_JPEG_REPROCESS

#define MAX_STREAM_NUM  6

class RequestManager : public LightRefBase<RequestManager>,
                       public CameraErrorListener
{
public:
    RequestManager(int cameraId);
    ~RequestManager();

    int initialize(CameraInfo& info);
    int setRequestOperation(const camera2_request_queue_src_ops_t *request_src_ops);
    int setFrameOperation(const camera2_frame_queue_dst_ops_t *frame_dst_ops);
    int CreateDefaultRequest(int request_template, camera_metadata_t **request);
    int allocateStream(uint32_t width,
                        uint32_t height, int format,
                        const camera2_stream_ops_t *stream_ops,
                        uint32_t *stream_id,
                        uint32_t *format_actual,
                        uint32_t *usage,
                        uint32_t *max_buffers);
    int registerStreamBuffers(uint32_t stream_id, int num_buffers,
                        buffer_handle_t *buffers);
    int releaseStream(uint32_t stream_id);
    int getInProcessCount();

    int dispatchRequest();
    bool handleRequest();
    void release();
    void setErrorListener(CameraErrorListener *listener);

    class RequestHandleThread : public Thread {
    public:
        RequestHandleThread(RequestManager *rm) :
            Thread(false), mRequestManager(rm) {}

        virtual bool threadLoop() {
            return mRequestManager->handleRequest();
        }

    private:
        RequestManager *mRequestManager;
    };

private:
    int tryRestartStreams(int requestType);
    void stopStream(int id);
    void stopAllStreams();
    bool isStreamValid(int requestType, int streamId, int videoSnap);
    void handleError(int err);

private:
    sp<DeviceAdapter>  mDeviceAdapter;
    sp<RequestHandleThread> mRequestThread;
    mutable Mutex mThreadLock;
    const camera2_request_queue_src_ops_t *mRequestOperation;
    const camera2_frame_queue_dst_ops_t *mFrameOperation;
    sp<MetadaManager> mMetadaManager;

    sp<StreamAdapter> mStreamAdapter[MAX_STREAM_NUM];
    mutable Mutex mStreamLock;
    uint8_t mPendingRequests;
    int mCameraId;
    CameraErrorListener *mErrorListener;
    bool mWorkInProcess;
};

#endif
