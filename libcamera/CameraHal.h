/*
 * Copyright 2009 Freescale Semiconductor, Inc. All Rights Reserved.
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
#include <utils/MemoryBase.h>
#include <utils/MemoryHeapBase.h>
#include <utils/threads.h>
#include <ui/CameraHardwareInterface.h>
#include <ui/Overlay.h>

#ifdef USE_FSL_JPEG_ENC
#include "jpeg_enc_interface.h" 
#endif
#include "CannedJpeg.h"

struct picbuffer
{
        unsigned char *start;
        size_t offset;
        unsigned int length;
};

namespace android {
class CameraHal : public CameraHardwareInterface {
public:
    virtual sp<IMemoryHeap> getPreviewHeap() const;
    virtual sp<IMemoryHeap> getRawHeap() const;

    virtual status_t    startPreview(preview_callback cb, void* user);
    virtual bool        useOverlay() { return true; }
    virtual status_t    setOverlay(const sp<Overlay> &overlay);
    virtual void        stopPreview();
    virtual bool        previewEnabled();

    virtual status_t    startRecording(recording_callback cb, void* user);
    virtual void        stopRecording();
    virtual bool        recordingEnabled();
    virtual void        releaseRecordingFrame(const sp<IMemory>& mem);

    virtual status_t    autoFocus(autofocus_callback, void *user);
    virtual status_t    takePicture(shutter_callback,
                                    raw_callback,
                                    jpeg_callback,
                                    void* user);
    virtual status_t    cancelPicture(bool cancel_shutter,
                                      bool cancel_raw,
                                      bool cancel_jpeg);
    virtual status_t    dump(int fd, const Vector<String16>& args) const;
    virtual status_t    setParameters(const CameraParameters& params);
    virtual CameraParameters  getParameters() const;
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

    class PreviewThread : public Thread {
        CameraHal* mHardware;
    public:
        PreviewThread(CameraHal* hw)
            : Thread(false), mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraPreviewThread", PRIORITY_URGENT_DISPLAY);
        }
        virtual bool threadLoop() {
            mHardware->previewThread();
            // loop until we need to quit
            return true;
        }
    };

    class RecordThread : public Thread {
        CameraHal* mHardware;
    public:
        RecordThread(CameraHal* hw)
            : Thread(false), mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraRecordThread", PRIORITY_URGENT_DISPLAY);
        }
        virtual bool threadLoop() {
            mHardware->recordThread();
            // loop until we need to quit
            return true;
        }
    };

    void initDefaultParameters();
    bool initHeapLocked();

    int previewThread();
    int recordThread();

    static int beginAutoFocusThread(void *cookie);
    int autoFocusThread();

    static int beginPictureThread(void *cookie);
    int pictureThread();

    int validateSize(int w, int h);
    void* cropImage(unsigned long buffer);
    void convertYUYVtoYUV422SP(uint8_t *inputBuffer, uint8_t *outputBuffer, int width, int height);
#if USE_FSL_JPEG_ENC
    void convertYUYVtoUYVY(uint8_t *inputBuffer, uint8_t *outputBuffer, int width, int height);
    sp<MemoryBase> encodeImage(void *buffer, uint32_t bufflen);
#else
    sp<MemoryBase> encodeImage(void *buffer, uint32_t bufflen);
#endif

    int CameraOpen();
    int CameraClose();
    int CameraPreviewConfig(int fd_v4l);
    int CameraRecordingConfig(int fd_v4l);
    int CameraStartRecording(int fd_v4l);
    int CameraTakePicConfig(int fd_v4l);
    int CameraGetFBInfo(void);

    int fcount;
    mutable Mutex       mLock;

    CameraParameters    mParameters;

    sp<MemoryHeapBase>  mHeap;
    sp<MemoryHeapBase>  mSurfaceFlingerHeap;
    sp<MemoryHeapBase>  mPictureHeap;
    sp<MemoryHeapBase>  mRawHeap;
    sp<MemoryBase>      mSurfaceFlingerBuffer;

    bool                mPreviewRunning;
    int                 mPreviewFrameSize;

    shutter_callback    mShutterCallback;
    raw_callback        mRawPictureCallback;
    jpeg_callback       mJpegPictureCallback;
    void                *mPictureCallbackCookie;
    
    // protected by mLock
    sp<Overlay>         mOverlay;
    sp<PreviewThread>   mPreviewThread;
    preview_callback    mPreviewCallback;
    void                *mPreviewCallbackCookie;

    bool                mCameraOpened;
    bool                mIsTakingPic;;

    sp<RecordThread>    mRecordThread;
    int 		mRecordFrameSize;
    bool                mRecordRunning;
    recording_callback  mRecordCallback;
    void                *mRecordCallbackCookie;
    int                 mCurrentRecordFrame;
    int 		nCameraBuffersQueued;
   
    static const int    videoBufferCount = 3;
    sp<MemoryHeapBase>  mVideoHeap;
    sp<MemoryBase>      mVideoBuffer[videoBufferCount];
    int   		mVideoBufferUsing[videoBufferCount];

    autofocus_callback  mAutoFocusCallback;
    void                *mAutoFocusCallbackCookie;

    // only used from PreviewThread
    int                 mCurrentPreviewFrame;

    bool previewStopped;
    bool recordStopped;
    bool doubledPreviewWidth;
    bool doubledPreviewHeight;

    static int camera_device;
    static int g_camera_framerate;

    //used for priview
    static int g_sensor_width;
    static int g_sensor_height;
    static int g_sensor_top;
    static int g_sensor_left;
    static int g_display_width;
    static int g_display_height;
    static int g_display_top;
    static int g_display_left;
    static int g_rotate;
    static int g_display_lcd;
    static int g_preview_width;
    static int g_preview_height;

    //used for taking picture
    static int g_pic_width;
    static int g_pic_height;
    static int g_still_bpp;

    //used for recording
    static int g_capture_mode;		//0:low resolution 1:high resolution
    static struct picbuffer buffers[3];

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

