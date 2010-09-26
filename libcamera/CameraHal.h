/*
 * Copyright 2009-2010 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * Copyright (C) Texas Instruments - http://www.ti.com/
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */


#ifndef ANDROID_HARDWARE_CAMERA_HARDWARE_H
#define ANDROID_HARDWARE_CAMERA_HARDWARE_H

#include <string.h>
#include <unistd.h>
#include <time.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/time.h>
#include <linux/videodev2.h>
#include <linux/mxcfb.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <utils/Log.h>
#include <utils/threads.h>
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <camera/CameraHardwareInterface.h>
#include <ui/Overlay.h>
#include <dirent.h>

#include <semaphore.h>


#define FB_DEVICE               "/dev/graphics/fb0"
#define MIN_WIDTH               176
#define MIN_HEIGHT              144

#define PREVIEW_FRAMERATE       30
#define PICTURE_WIDTH           640     //default picture width
#define PICTURE_HEIGHT          480     //default picture height

#define RECORDING_WIDTH_NORMAL  352    //default recording width
#define RECORDING_HEIGHT_NORMAL 288     //default recording height

#define CAPTURE_BUFFER_NUM      3
#define VIDEO_OUTPUT_BUFFER_NUM	3

#define LOG_FUNCTION_NAME       LOGD("%d: %s() Executing...", __LINE__, __FUNCTION__);

//#define UVC_CAMERA              1

#define PARAM_BUFFER 		512
#define FILENAME_LENGTH		256

#ifdef USE_FSL_JPEG_ENC
#include "jpeg_enc_interface.h" 
#endif
#include "CannedJpeg.h"

struct picbuffer
{
    unsigned char *virt_start;
    size_t phy_offset;
    unsigned int length;
};

namespace android {
class CameraHal : public CameraHardwareInterface {
public:
    virtual sp<IMemoryHeap> getPreviewHeap() const;
    virtual sp<IMemoryHeap> getRawHeap() const;

    virtual void        setCallbacks(notify_callback notify_cb,
                                     data_callback data_cb,
                                     data_callback_timestamp data_cb_timestamp,
                                     void* user);

    virtual void        enableMsgType(int32_t msgType);
    virtual void        disableMsgType(int32_t msgType);
    virtual bool        msgTypeEnabled(int32_t msgType);

    virtual bool        useOverlay() { return true; }
    virtual status_t    setOverlay(const sp<Overlay> &overlay);

    virtual status_t    startPreview();
    virtual void        stopPreview();
    virtual bool        previewEnabled();

    virtual status_t    startRecording();
    virtual void        stopRecording();
    virtual bool        recordingEnabled();
    virtual void        releaseRecordingFrame(const sp<IMemory>& mem);

    virtual status_t    autoFocus();
    virtual status_t    cancelAutoFocus();
    virtual status_t    takePicture();
    virtual status_t    cancelPicture();
    virtual status_t    dump(int fd, const Vector<String16>& args) const;
    virtual status_t    setParameters(const CameraParameters& params);
    virtual CameraParameters  getParameters() const;
    virtual status_t    sendCommand(int32_t command, int32_t arg1,
                                    int32_t arg2);
    virtual void release();

    static sp<CameraHardwareInterface> createInstance();
    
#ifdef USE_FSL_JPEG_ENC   
    static JPEG_ENC_UINT8 pushJpegOutput(JPEG_ENC_UINT8 ** out_buf_ptrptr,
                                            JPEG_ENC_UINT32 *out_buf_len_ptr,
                                            JPEG_ENC_UINT8 flush, 
                                            void * context, 
                                            JPEG_ENC_MODE enc_mode);
#endif

private:
                        CameraHal();
    virtual             ~CameraHal();

    static wp<CameraHardwareInterface> singleton;

    class PreviewShowFrameThread : public Thread {
        CameraHal* mHardware;
    public:
        PreviewShowFrameThread(CameraHal* hw)
            : Thread(false), mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraPreviewShowFrameThread", PRIORITY_URGENT_DISPLAY);
        }
        virtual bool threadLoop() {
            mHardware->previewShowFrameThread();
            // loop until we need to quit
            return true;
        }
    };

    class PreviewCaptureFrameThread : public Thread {
        CameraHal* mHardware;
    public:
        PreviewCaptureFrameThread(CameraHal* hw)
            : Thread(false), mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraPreviewCaptureFrameThread", PRIORITY_URGENT_DISPLAY);
        }
        virtual bool threadLoop() {
            mHardware->previewCaptureFrameThread();
            // loop until we need to quit
            return true;
        }
    };

    void initDefaultParameters();
    bool initHeapLocked();

    int previewShowFrameThread();
    int previewCaptureFrameThread();

    static int beginAutoFocusThread(void *cookie);
    int autoFocusThread();

    static int beginPictureThread(void *cookie);
    int pictureThread();

    int validateSize(int w, int h);
    void* cropImage(unsigned long buffer);

    void convertYUYVtoYUV420SP(uint8_t *inputBuffer, uint8_t *outputBuffer, int width, int height);
    int uvcGetDeviceAndCapability(char *sizes_buf);

    sp<MemoryBase> encodeImage(void *buffer, uint32_t bufflen);

    int cameraOpen();
    int cameraClose();

    int cameraDestroy();
    int cameraPreviewConfig();
    int cameraPreviewStart();
    void cameraPreviewStop();
    int cameraTakePicConfig();
    int cameraTakePicture();
    void previewOneFrame();

    mutable Mutex       mLock;

    CameraParameters    mParameters;

    sp<MemoryHeapBase>  mPreviewHeap;
    sp<MemoryBase>      mPreviewBuffers[CAPTURE_BUFFER_NUM]; // used when UVC camera
    bool                mPreviewRunning;
    int                 mRecordHeight;
    int                 mRecordWidth;
    int                 mRecordFormat;

    // protected by mLock
    sp<Overlay>         mOverlay;
    sp<PreviewShowFrameThread>   mPreviewShowFrameThread;
    sp<PreviewCaptureFrameThread> mPreviewCaptureFrameThread;

    notify_callback    mNotifyCb;
    data_callback      mDataCb;
    data_callback_timestamp mDataCbTimestamp;
    void               *mCallbackCookie;

    int32_t             mMsgEnabled;

    bool                mCameraOpened;

    int                 mPictureHeight;
    int                 mPictureWidth;
    int                 mPictureFormat;

    int 		mRecordFrameSize;
    bool                mRecordRunning;
    int                 mCurrentRecordFrame;
    int 		nCameraBuffersQueued;

    /* These buffers are used to output captured video to upper layer,
       eg, for to use video encoder */
    sp<MemoryHeapBase>  mVideoHeap;
    sp<MemoryBase>      mVideoBuffers[VIDEO_OUTPUT_BUFFER_NUM];
    int   		mVideoBufferUsing[VIDEO_OUTPUT_BUFFER_NUM];

    bool previewStopped;
    bool recordStopped;
    bool doubledPreviewWidth;
    bool doubledPreviewHeight;

    static int camera_device;
    static int g_camera_framerate;
    static char dev_node[FILENAME_LENGTH];

    static int g_rotate;
    static int g_still_bpp;

    //used for recording
    struct picbuffer mCaptureBuffers[CAPTURE_BUFFER_NUM];
    int    buffer_index_maps[CAPTURE_BUFFER_NUM];

    static const char supportedPictureSizes[];
    static const char supportedPreviewSizes[];
    static const char supportedFPS[];
    static const char supprotedThumbnailSizes[];
    static const char PARAMS_DELIMITER[];

    int error_status;
    int display_head;
    int dequeue_head;
    int is_first_buffer;
    int is_overlay_pushmode;
    int last_display_index;

    sem_t avaiable_show_frame;
    sem_t avaible_dequeue_frame;
    pthread_mutex_t mOverlay_sem;

#ifdef DUMP_CAPTURE_YUV
    static FILE *record_yuvFile;
    static FILE *capture_yuvFile;
#endif

#ifdef USE_FSL_JPEG_ENC 
    static JPEG_ENC_UINT32 g_JpegDataSize;//Total size of g_JpegData
    static JPEG_ENC_UINT32 g_JpegDataLen;//Valid data len of g_JpegData
    static JPEG_ENC_UINT8 *g_JpegData;//Buffer to hold jpeg data
#endif
};

}; // namespace android

#endif

