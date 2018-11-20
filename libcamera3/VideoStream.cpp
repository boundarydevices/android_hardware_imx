/*
 * Copyright (C) 2015-2016 Freescale Semiconductor, Inc.
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

#include "VideoStream.h"

using namespace android;

VideoStream::VideoStream(Camera* device)
    : Stream(device), mState(STATE_INVALID),
      mChanged(false), mDev(-1),
      mAllocatedBuffers(0),
      mOmitFrames(0), mOmitFrmCount(0)
{
    mV4l2MemType =  V4L2_MEMORY_MMAP;
    mV4l2BufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    mMessageThread = new MessageThread(this);
}

VideoStream::~VideoStream()
{
    ALOGI("%s", __func__);
    destroyStream();
    mMessageQueue.clearMessages();
    mMessageQueue.clearCommands();
    mMessageThread.clear();
    mMessageThread = NULL;
}

void VideoStream::destroyStream()
{
    ALOGI("%s", __func__);
    {
        Mutex::Autolock lock(mLock);
        if (mState == STATE_ERROR) {
            ALOGI("%s finished!", __func__);
            return;
        }
    }

    if (mMessageThread != NULL && mMessageThread->isRunning()) {
        mMessageQueue.postMessage(new CMessage(MSG_EXIT, 1), 1);
        mMessageThread->requestExit();
        mMessageThread->join();
    }
    ALOGI("%s finished!!", __func__);
}

int32_t VideoStream::openDev(const char* name)
{
    ALOGI("%s", __func__);
    if (name == NULL) {
        ALOGE("invalid dev name");
        return BAD_VALUE;
    }

    Mutex::Autolock lock(mLock);

    mDev = open(name, O_RDWR);
    if (mDev <= 0) {
        ALOGE("%s can not open camera devpath:%s", __func__, name);
        return BAD_VALUE;
    }

    return 0;
}

int32_t VideoStream::configure(sp<Stream> stream)
{
    ALOGV("%s", __func__);
    if ((stream->width() == 0) || (stream->height() == 0)
         || (stream->format() == 0)) {
        ALOGE("%s: invalid stream parameters", __func__);
        return BAD_VALUE;
    }

    int32_t sensorFormat = mCamera->getSensorFormat(stream->format());

    Mutex::Autolock lock(mLock);

    ConfigureParam* params = new ConfigureParam();
    params->mWidth  = stream->width();
    params->mHeight = stream->height();
    params->mFormat = sensorFormat;
    params->mFps = stream->fps();
    params->mBuffers = stream->bufferNum();
    params->mIsJpeg = stream->isJpeg();

    ALOGI("%s: w:%d, h:%d, sensor format:0x%x, stream format:0x%x, fps:%d, num:%d",
           __func__, params->mWidth, params->mHeight, params->mFormat, stream->format(), params->mFps, params->mBuffers);
    mMessageQueue.postMessage(new CMessage(MSG_CONFIG, (uintptr_t)params), 0);

    return 0;
}

int32_t VideoStream::handleConfigureLocked(ConfigureParam* params)
{
    int32_t ret   = 0;
    ALOGV("%s", __func__);

    if (params == NULL) {
        ALOGW("%s invalid params", __func__);
        return 0;
    }

    // when width&height&format are same, keep it to reduce start/stop time.
    if ((mWidth == params->mWidth) && (mHeight == params->mHeight)
         && (mFormat == params->mFormat)  && (params->mIsJpeg || (mFps == params->mFps))) {
        ALOGI("%s, same config, res %dx%d, fmt 0x%x, fps %d, %d, jpg %d",
          __func__, mWidth, mHeight, mFormat, mFps, params->mFps, params->mIsJpeg);
        flushDevLocked();
        return 0;
    }

    mChanged = true;

    // add start state to go into config state.
    // so, only call config to do stop automically.
    if (mState == STATE_START) {
        ret = handleStopLocked(false);
        if (ret < 0) {
            ALOGE("please stop firstly before configure");
            return ret;
        }
    }

    // only invalid&stop&config state can go into config state.
    if ((mState != STATE_INVALID) && (mState != STATE_STOP) &&
        (mState != STATE_CONFIG)) {
        ALOGE("invalid state:0x%x go into config state", mState);
        return 0;
    }

    mWidth = params->mWidth;
    mHeight = params->mHeight;
    mFormat = params->mFormat;
    mFps = params->mFps;
    mNumBuffers = params->mBuffers;

    ret = onDeviceConfigureLocked();
    if (ret != 0) {
        ALOGE("%s onDeviceConfigure failed", __func__);
        return ret;
    }

    mState = STATE_CONFIG;

    return 0;
}

int32_t VideoStream::handleStartLocked(bool force)
{
    int32_t ret = 0;
    ALOGV("%s", __func__);

    // only config&stop state can go into start state.
    if ((mState != STATE_CONFIG) && (mState != STATE_STOP)) {
        ALOGE("invalid state:0x%x go into start state", mState);
        return 0;
    }

    if (mChanged || force) {
        mChanged = false;
        if (allocateBuffersLocked() != 0) {
            ALOGE("%s allocateBuffersLocked failed", __func__);
            return -1;
        }
    }

    ret = onDeviceStartLocked();
    if (ret != 0) {
        mState = STATE_ERROR;
        ALOGE("%s onDeviceStart failed", __func__);
        return ret;
    }

    mState = STATE_START;

    return 0;
}

int32_t VideoStream::closeDev()
{
    ALOGI("%s", __func__);
    Mutex::Autolock lock(mLock);

    if (mState != STATE_ERROR && mMessageThread->isRunning()) {
        mMessageQueue.postMessage(new CMessage(MSG_CLOSE, 0), 1);
    }
    else {
        ALOGI("%s thread is exit", __func__);
        if (mDev > 0) {
            close(mDev);
            mDev = -1;
        }
    }

    return 0;
}

int32_t VideoStream::handleStopLocked(bool force)
{
    int32_t ret = 0;
    ALOGV("%s", __func__);

    // only start can go into stop state.
    if (mState != STATE_START) {
        ALOGI("state:0x%x can't go into stop state", mState);
        return 0;
    }

    ret = onDeviceStopLocked();
    if (ret < 0) {
        mState = STATE_ERROR;
        ALOGE("StopStreaming: Unable to stop capture: %s", strerror(errno));
    }
    else {
        mState = STATE_STOP;
    }

    if (force || mChanged) {
        ret = freeBuffersLocked();
        if (ret != 0) {
            ALOGE("%s freeBuffersLocked failed", __func__);
            return -1;
        }
    }

    if (force) {
        // clear request messages.
        mMessageQueue.clearMessages();
        // clear capture request.
        mRequests.clear();
        // to do configure agian.
        mWidth = 0;
    }

    return ret;
}

int32_t VideoStream::flushDev()
{
    Mutex::Autolock lock(mLock);
    return flushDevLocked();
}

int32_t VideoStream::flushDevLocked()
{
    if (mState != STATE_ERROR && mMessageThread->isRunning()) {
       mMessageQueue.postMessage(new CMessage(MSG_FLUSH, 0), 1);
    } else {
        ALOGI("%s flushDev failed", __func__);
        return -1;
    }

    return 0;
}

int32_t VideoStream::handleFlushLocked() {
    if (mState != STATE_START) {
        ALOGI("state:0x%x flushing can be done only in the start state", mState);
        return 0;
    }

    onFlushLocked();
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

int32_t VideoStream::requestCapture(sp<CaptureRequest> req)
{
    Mutex::Autolock lock(mLock);

    mRequests.push_back(req);

    mMessageQueue.postMessage(new CMessage(MSG_FRAME, 0));

    return 0;
}

StreamBuffer* VideoStream::acquireFrameLocked()
{
    int32_t index = onFrameAcquireLocked();
    if (index >= MAX_STREAM_BUFFERS || index < 0) {
        ALOGE("%s: invalid index %d", __func__, index);
        return NULL;
    }

    return mBuffers[index];
}

int32_t VideoStream::getBufferIndexLocked(StreamBuffer& buf)
{
    for (uint32_t i=0; i<mNumBuffers; i++) {
        if (mBuffers[i]->mPhyAddr == buf.mPhyAddr) {
            return i;
        }
    }

    return -1;
}

int32_t VideoStream::returnFrameLocked(StreamBuffer& buf)
{
    ALOGV("%s", __func__);
    int32_t i = getBufferIndexLocked(buf);
    if (i < 0 || i >= MAX_STREAM_BUFFERS) {
        return BAD_VALUE;
    }

    return onFrameReturnLocked(i, buf);
}

int32_t VideoStream::handleCaptureFrame()
{
    int32_t ret = 0;
    ALOGV("%s", __func__);

    List< sp<CaptureRequest> >::iterator cur;
    sp<CaptureRequest> req = NULL;
    StreamBuffer *buf = NULL;
    {
        Mutex::Autolock lock(mLock);
        if (mRequests.empty()) {
            return 0;
        }

        cur = mRequests.begin();
        req = *cur;
    }
    //advanced character.
    ret = processCaptureSettings(req);
    if (ret != 0) {
        Mutex::Autolock lock(mLock);
        mRequests.erase(cur);
        ALOGE("processSettings failed");
        return 0;
    }

    {
        Mutex::Autolock lock(mLock);
        buf = acquireFrameLocked();
    }

    if (buf == NULL) {
        ALOGE("acquireFrameLocked failed");
        req->onCaptureError();
        Mutex::Autolock lock(mLock);
        mRequests.erase(cur);
        return 0;
    }

    ret = processCaptureRequest(*buf, req);
    if (ret != 0) {
        Mutex::Autolock lock(mLock);
        returnFrameLocked(*buf);
        ALOGE("processRequest failed");
        return 0;
    }

    Mutex::Autolock lock(mLock);
    mRequests.erase(cur);
    returnFrameLocked(*buf);

    return 0;
}

int32_t VideoStream::processCaptureRequest(StreamBuffer& src,
                         sp<CaptureRequest> req)
{
    int32_t ret = 0;
    ALOGV("%s", __func__);
    for (uint32_t i=0; i<req->mOutBuffersNumber; i++) {
        StreamBuffer* out = req->mOutBuffers[i];
        sp<Stream>& stream = out->mStream;
        // stream to process buffer.
        stream->setCurrentBuffer(out);
        stream->processCaptureBuffer(src, req->mSettings);
        stream->setCurrentBuffer(NULL);
        ret = req->onCaptureDone(out);
        if (ret != 0) {
            return ret;
        }
    }

    return ret;
}

// process advanced character.
int32_t VideoStream::processCaptureSettings(sp<CaptureRequest> req)
{
    ALOGV("%s", __func__);
    sp<Metadata> meta = req->mSettings;
    if (meta == NULL || meta->get() == NULL) {
        ALOGI("invalid meta data");
        return 0;
    }
    // device to do advanced character set.
    int32_t ret = mCamera->processSettings(meta, req->mFrameNumber);
    if (ret != 0) {
        ALOGI("mCamera->processSettings failed");
        return ret;
    }

    ret = req->onSettingsDone(meta);
    if (ret != 0) {
        ALOGI("onSettingsDone failed");
        return ret;
    }

    if (req->mOutBuffersNumber == 0) {
        ALOGI("num_output_buffers less than 0");
        ret = 1;
    }

    return ret;
}

int32_t VideoStream::handleMessage()
{
    int32_t ret = 0;

    sp<CMessage> msg = mMessageQueue.waitMessage();
    if (msg == 0) {
        ALOGE("get invalid message");
        return -1;
    }

    switch (msg->what) {
        case MSG_CONFIG: {
            Mutex::Autolock lock(mLock);
            ConfigureParam* params = (ConfigureParam*)(uintptr_t)msg->arg0;
            ret = handleConfigureLocked(params);
            if (params != NULL) {
                delete params;
            }
        }
        break;

        case MSG_CLOSE: {
            Mutex::Autolock lock(mLock);
            ret = handleStopLocked(true);
            if (mDev > 0) {
                close(mDev);
                mDev = -1;
            }
        }
        break;

        case MSG_FRAME: {
            Mutex::Autolock lock(mLock);
            // to start device automically.
            if (mState != STATE_START) {
                ALOGV("state:0x%x when handle frame message", mState);
                ret = handleStartLocked(false);
                if (ret != 0) {
                    ALOGE("%s handleStartLocked failed", __func__);
                    return ret;
                }
            }

        }
        ret = handleCaptureFrame();
        break;

        case MSG_EXIT: {
            Mutex::Autolock lock(mLock);
            ALOGI("capture thread exit...");
            if (mState == STATE_START) {
                handleStopLocked(true);
            }

            ret = -1;
        }
        break;

        case MSG_FLUSH: {
            Mutex::Autolock lock(mLock);
            if (mState == STATE_START) {
                handleFlushLocked();
            }
        }
        break;

        default: {
            ALOGE("%s invalid message what:%d", __func__, msg->what);
        }
        break;
    }

    return ret;
}

