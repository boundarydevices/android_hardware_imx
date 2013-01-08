/*
 * Copyright (C) 2009-2013 Freescale Semiconductor, Inc.
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

#ifndef _CAMERA_HAL_H
#define _CAMERA_HAL_H

#include "CameraUtil.h"
#include "DeviceAdapter.h"
#include "RequestManager.h"
#include <hardware/camera2.h>

using namespace android;

class PhysMemAdapter;

class CameraHal : public CameraErrorListener
{
public:
    CameraHal(int cameraId);
    ~CameraHal();
    status_t initialize(CameraInfo& info);
    void handleError(int err);

    //camera2 interface.
    int set_request_queue_src_ops(
        const camera2_request_queue_src_ops_t *request_src_ops);
    int notify_request_queue_not_empty();
    int set_frame_queue_dst_ops(const camera2_frame_queue_dst_ops_t *frame_dst_ops);
    int get_in_progress_count();
    int construct_default_request(int request_template, camera_metadata_t **request);
    int allocate_stream(uint32_t width,
        uint32_t height, int format,
        const camera2_stream_ops_t *stream_ops,
        uint32_t *stream_id,
        uint32_t *format_actual,
        uint32_t *usage,
        uint32_t *max_buffers);
    int register_stream_buffers(uint32_t stream_id, int num_buffers,
        buffer_handle_t *buffers);
    int release_stream(uint32_t stream_id);
    int allocate_reprocess_stream(
        uint32_t width,
        uint32_t height,
        uint32_t format,
        const camera2_stream_in_ops_t *reprocess_stream_ops,
        uint32_t *stream_id,
        uint32_t *consumer_usage,
        uint32_t *max_buffers);
    int release_reprocess_stream(
        uint32_t stream_id);
    int get_metadata_vendor_tag_ops(vendor_tag_query_ops_t **ops);
    int set_notify_callback(camera2_notify_callback notify_cb,
            void *user);

    void     release();
    status_t dump(int fd) const;

    void     LockWakeLock();
    void     UnLockWakeLock();

private:
    bool mPowerLock;
    int  mCameraId;
    mutable Mutex mLock;

private:
    sp<RequestManager> mRequestManager;
    const camera2_request_queue_src_ops *mRequestQueue;
    const camera2_frame_queue_dst_ops *mFrameQueue;
    camera2_notify_callback mNotifyCb;
    void *mNotifyUserPtr;
};

#endif // ifndef _CAMERA_HAL_H
