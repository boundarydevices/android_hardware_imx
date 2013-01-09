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

#include "RequestManager.h"

RequestManager::RequestManager(int cameraId)
{
    mRequestOperation = NULL;
    mPendingRequests = 0;
    mCameraId = cameraId;
    mErrorListener = NULL;
    mWorkInProcess = false;
}

RequestManager::~RequestManager()
{
    release();
}

int RequestManager::initialize(CameraInfo& info)
{
    status_t ret = NO_ERROR;

    FLOG_RUNTIME("initialize name:%s, path:%s", info.name, info.devPath);
    mDeviceAdapter = DeviceAdapter::Create(info);
    if (mDeviceAdapter == NULL) {
        FLOGE("CameraHal: DeviceAdapter create failed");
        return BAD_VALUE;
    }

    mMetadaManager = new MetadaManager(mDeviceAdapter.get(), mCameraId);
    mDeviceAdapter->setMetadaManager(mMetadaManager);
    ret = mDeviceAdapter->initialize(info);
    if (ret) {
        FLOGE("CameraHal: DeviceAdapter initialize failed");
        return ret;
    }

    ret = mMetadaManager->createStaticInfo(
               (camera_metadata_t**)&info.static_camera_characteristics, true);
    if (ret) {
        FLOGE("CameraHal: createStaticInfo failed");
        return ret;
    }

    ret = mMetadaManager->createStaticInfo(
               (camera_metadata_t**)&info.static_camera_characteristics, false);
    if (ret) {
        FLOGE("CameraHal: createStaticInfo 2 failed");
        return ret;
    }
    mPendingRequests=0;
    mWorkInProcess = true;

    return ret;
}

void RequestManager::setErrorListener(CameraErrorListener *listener)
{
    mErrorListener = listener;
    if (mDeviceAdapter.get() != NULL) {
        mDeviceAdapter->setErrorListener(this);
    }
}

void RequestManager::handleError(int err)
{
    mWorkInProcess = false;
    if (mErrorListener != NULL) {
        mErrorListener->handleError(err);
    }
}

void RequestManager::stopStream(int id)
{
    sp<StreamAdapter> cameraStream = mStreamAdapter[id];
    FLOG_RUNTIME("%s steam id:%d", __FUNCTION__, id);
    if (cameraStream.get() != NULL) {
        if (cameraStream->mStarted) {
            cameraStream->stop();
        }
        if (cameraStream->mPrepared) {
            cameraStream->release();
        }
    }
    FLOG_RUNTIME("%s end", __FUNCTION__);
}

void RequestManager::stopAllStreams()
{
    FLOG_TRACE("%s running", __FUNCTION__);
    for (int id = 0; id < MAX_STREAM_NUM; id++) {
        stopStream(id);
    }
    FLOG_TRACE("%s end", __FUNCTION__);
}

int RequestManager::setRequestOperation(const camera2_request_queue_src_ops_t *request_src_ops)
{
    mRequestOperation = request_src_ops;
    return 0;
}

int RequestManager::setFrameOperation(const camera2_frame_queue_dst_ops_t *frame_dst_ops)
{
    mFrameOperation = frame_dst_ops;
    return 0;
}

int RequestManager::CreateDefaultRequest(int request_template, camera_metadata_t **request)
{
    FLOG_TRACE("DEBUG(%s): making template (%d) ", __FUNCTION__, request_template);

    if (request == NULL) return BAD_VALUE;
    if (request_template < 0 || request_template >= CAMERA2_TEMPLATE_COUNT) {
        return BAD_VALUE;
    }
    status_t res;
    fAssert(mMetadaManager.get() != NULL);
    // Pass 1, calculate size and allocate
    res = mMetadaManager->createDefaultRequest(request_template,
            request,
            true);
    if (res != OK) {
        return res;
    }
    // Pass 2, build request
    res = mMetadaManager->createDefaultRequest(request_template,
            request,
            false);
    if (res != OK) {
        FLOGE("Unable to populate new request for template %d",
                request_template);
    }

    return res;
}

int RequestManager::dispatchRequest()
{
    FLOG_TRACE("%s running", __FUNCTION__);
    if (mRequestThread.get() != NULL) {
        FLOGI("RequestThread is running, request it exit");
        mRequestThread->requestExit();
        FLOGI("RequestThread exiting");
    }

    mRequestThread = new RequestHandleThread(this);
    mPendingRequests++;
    mRequestThread->run("RequestHandle", PRIORITY_DEFAULT);
    return 0;
}

bool RequestManager::handleRequest()
{
    FLOG_TRACE("%s running", __FUNCTION__);
    int res;
    camera_metadata_t *request=NULL;

    while(mRequestOperation && mWorkInProcess) {
        FLOG_RUNTIME("%s:Dequeue request" ,__FUNCTION__);
        mRequestOperation->dequeue_request(mRequestOperation, &request);
        if(request == NULL) {
            FLOGE("%s:No more requests available", __FUNCTION__);
            break;
        }

        /* Check the streams that need to be active in the stream request */
        sort_camera_metadata(request);

        res = mMetadaManager->setCurrentRequest(request);
        if (res != NO_ERROR) {
            FLOGE("%s: setCurrentRequest failed", __FUNCTION__);
            mPendingRequests--;
            return false;
        }

        int requestType = 0;
        mMetadaManager->getRequestType(&requestType);
        FLOG_RUNTIME("%s:start request %d", __FUNCTION__, requestType);

        int numEntries = 0;
        int frameSize = 0;
        numEntries = get_camera_metadata_entry_count(request);
        frameSize = get_camera_metadata_size(request);
        camera_metadata_t *currentFrame = NULL;
        res = mFrameOperation->dequeue_frame(mFrameOperation, numEntries,
                               frameSize, &currentFrame);
        if (res < 0) {
            FLOGE("%s: dequeue_frame failed", __FUNCTION__);
            currentFrame = NULL;
        }
        else {
            res = mMetadaManager->generateFrameRequest(currentFrame);
            if (res == 0) {
                mFrameOperation->enqueue_frame(mFrameOperation, currentFrame);
            }
            else {
                mFrameOperation->cancel_frame(mFrameOperation, currentFrame);
            }
        }

        res = tryRestartStreams(requestType);
        if (res != NO_ERROR) {
            FLOGE("%s: tryRestartStreams failed", __FUNCTION__);
            mPendingRequests--;
            return false;
        }

        /* Free the request buffer */
        mRequestOperation->free_request(mRequestOperation, request);
        FLOG_RUNTIME("%s:Completed request %d", __FUNCTION__, requestType);
    }//end while

    FLOG_TRACE("%s exiting", __FUNCTION__);
    stopAllStreams();
    mRequestThread.clear();
    mPendingRequests--;
    FLOG_TRACE("%s end...", __FUNCTION__);

    return false;
}

bool RequestManager::isStreamValid(int requestType, int streamId, int videoSnap)
{
    if (videoSnap) {
        return true;
    }

    if (requestType == REQUEST_TYPE_CAPTURE && streamId == STREAM_ID_PREVIEW) {
        return false;
    }

    if (requestType == REQUEST_TYPE_CAPTURE && streamId == STREAM_ID_PRVCB) {
        return false;
    }

    if (requestType == REQUEST_TYPE_PREVIEW && streamId == STREAM_ID_JPEG) {
        return false;
    }

    return true;
}

int RequestManager::tryRestartStreams(int requestType)
{
    FLOG_RUNTIME("%s running", __FUNCTION__);
    int res = 0;
    int fps = 30;
    res = mMetadaManager->getFrameRate(&fps);
    if (res != NO_ERROR) {
        FLOGE("%s: getFrameRate failed", __FUNCTION__);
        return res;
    }

    camera_metadata_entry_t streams;
    res = mMetadaManager->getRequestStreams(&streams);
    if (res != NO_ERROR) {
        FLOGE("%s: getRequestStreams failed", __FUNCTION__);
        return res;
    }

    bool streamRecord = false;
    bool streamPicture = false;
    bool videoSnapshot = false;
    for (uint32_t i = 0; i < streams.count; i++) {
        int streamId = streams.data.u8[i];
        if (streamId == STREAM_ID_RECORD) {
            streamRecord = true;
        }
        else if (streamId == STREAM_ID_JPEG) {
            streamPicture = true;
        }
    }

    if (streamRecord && streamPicture) {
        videoSnapshot = true;
    }

    for (int id = 0; id < MAX_STREAM_NUM; id++) {
        sp<StreamAdapter> stream = mStreamAdapter[id];
        if (!isStreamValid(requestType, id, videoSnapshot)) {
            if (stream.get() != NULL && stream->mPrepared) {
                FLOGI("%s stop unused stream %d", __FUNCTION__, id);
                stopStream(id);
            }
        }
    }

    for (uint32_t i = 0; i < streams.count; i++) {
        int streamId = streams.data.u8[i];
        if (!isStreamValid(requestType, streamId, videoSnapshot)) {
            continue;
        }
        sp<StreamAdapter> stream = mStreamAdapter[streamId];
        if (stream.get() == NULL) {
            continue;
        }

        if (!stream->mPrepared) {
            res = stream->configure(fps, videoSnapshot);
            if (res != NO_ERROR) {
                FLOGE("error configure stream %d", res);
                return res;
            }
        }

        if (!stream->mStarted) {
            res = stream->start();
            if (res != NO_ERROR) {
                FLOGE("error start stream %d", res);
                return res;
            }
        }
    }

    for (uint32_t i = 0; i < streams.count; i++) {
        int streamId = streams.data.u8[i];
        if (!isStreamValid(requestType, streamId, videoSnapshot)) {
            continue;
        }

        sp<StreamAdapter> stream = mStreamAdapter[streamId];
        if (stream.get() == NULL) {
            continue;
        }

        if (!stream->mPrepared || !stream->mStarted) {
            continue;
        }

        stream->applyRequest();
    }

    return res;
}

int RequestManager::getInProcessCount()
{
    return mPendingRequests;
}

int RequestManager::allocateStream(uint32_t width,
        uint32_t height, int format,
        const camera2_stream_ops_t *stream_ops,
        uint32_t *stream_id,
        uint32_t *format_actual,
        uint32_t *usage,
        uint32_t *max_buffers)
{
    int sid = -1;
    sp<StreamAdapter> cameraStream;
    *usage = CAMERA_GRALLOC_USAGE;

    FLOG_TRACE("RequestManager %s...", __FUNCTION__);
    if (format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
        if(mStreamAdapter[STREAM_ID_PREVIEW].get() != NULL) {
            FLOGI("%s record stream, w:%d, h:%d, fmt:0x%x", __FUNCTION__,
                          width, height, format);
            sid = STREAM_ID_RECORD;
            cameraStream = new StreamAdapter(sid);
        }
        else {
            FLOGI("%s preview stream, w:%d, h:%d, fmt:0x%x", __FUNCTION__,
                          width, height, format);
            sid = STREAM_ID_PREVIEW;
            cameraStream = new PreviewStream(sid);
        }

        //*format_actual = HAL_PIXEL_FORMAT_YCrCb_420_SP;
        *format_actual = mDeviceAdapter->getPreviewPixelFormat();
        FLOGI("actual format 0x%x", *format_actual);
        *max_buffers = NUM_PREVIEW_BUFFER;
    }
    else if (format == HAL_PIXEL_FORMAT_BLOB) {
        FLOGI("%s jpeg stream, w:%d, h:%d, fmt:0x%x", __FUNCTION__,
                      width, height, format);
        //*format_actual = HAL_PIXEL_FORMAT_BLOB;
        *format_actual = mDeviceAdapter->getPicturePixelFormat();
        FLOGI("actual format 0x%x", *format_actual);
        sid = STREAM_ID_JPEG;
        *max_buffers = NUM_CAPTURE_BUFFER;

        cameraStream = new CaptureStream(sid);
    }
    else if (format == HAL_PIXEL_FORMAT_YCbCr_420_SP ||
                         format == HAL_PIXEL_FORMAT_YCbCr_420_P) {
        FLOGI("%s callback stream, w:%d, h:%d, fmt:0x%x", __FUNCTION__,
                      width, height, format);
        *format_actual = format;
        sid = STREAM_ID_PRVCB;
        *max_buffers = NUM_PREVIEW_BUFFER;

        cameraStream = new StreamAdapter(sid);
    }
    else if (format == CAMERA2_HAL_PIXEL_FORMAT_ZSL) {
        FLOGI("%s callback stream, w:%d, h:%d, fmt:0x%x", __FUNCTION__,
                      width, height, format);
        *format_actual = HAL_PIXEL_FORMAT_YCbCr_420_SP;
        sid = STREAM_ID_PRVCB;
        *max_buffers = NUM_PREVIEW_BUFFER;

        cameraStream = new StreamAdapter(sid);
    }
    else {
        FLOGE("format %d does not support now.", format);
        return BAD_VALUE;
    }

    *stream_id = sid;
    cameraStream->initialize(width, height, *format_actual, *usage, *max_buffers);
    cameraStream->setPreviewWindow(stream_ops);
    cameraStream->setDeviceAdapter(mDeviceAdapter);
    cameraStream->setMetadaManager(mMetadaManager);
    cameraStream->setErrorListener(this);

    mStreamAdapter[sid] = cameraStream;
    FLOG_TRACE("RequestManager %s end...", __FUNCTION__);

    return 0;
}

int RequestManager::registerStreamBuffers(uint32_t stream_id, int num_buffers,
        buffer_handle_t *buffers)
{
    FLOG_TRACE("RequestManager %s stream id:%d", __FUNCTION__, stream_id);
    fAssert(mStreamAdapter[stream_id].get() != NULL);
    mStreamAdapter[stream_id]->registerBuffers(num_buffers, buffers);
    FLOG_TRACE("RequestManager %s end...", __FUNCTION__);

    return 0;
}

int RequestManager::releaseStream(uint32_t stream_id)
{
    FLOG_TRACE("RequestManager %s stream id:%d", __FUNCTION__, stream_id);
    sp<StreamAdapter> cameraStream = mStreamAdapter[stream_id];
    if (cameraStream.get() == NULL) {
        FLOGI("%s release invalid stream %d", __FUNCTION__, stream_id);
        return 0;
    }

    if (cameraStream->mStarted) {
        cameraStream->stop();
    }
    if (cameraStream->mPrepared) {
        cameraStream->release();
    }
    mStreamAdapter[stream_id].clear();
    FLOG_TRACE("RequestManager %s end...", __FUNCTION__);

    return 0;
}

void RequestManager::release()
{
    FLOG_TRACE("RequestManager %s...", __FUNCTION__);
    for (int id = 0; id < MAX_STREAM_NUM; id++) {
        if (mStreamAdapter[id].get() != NULL) {
            releaseStream(id);
        }
    }
    FLOG_TRACE("RequestManager %s end...", __FUNCTION__);
}
