/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
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

#ifndef _UVC_DEVICE_H
#define _UVC_DEVICE_H

#include "CameraUtil.h"

#define DEFAULT_PREVIEW_FPS (15)
#define DEFAULT_PREVIEW_W   (640)
#define DEFAULT_PREVIEW_H   (480)
#define DEFAULT_PICTURE_W   (640)
#define DEFAULT_PICTURE_H   (480)
#define MAX_SENSOR_FORMAT 20
#define FORMAT_STRING_LEN 64


class UvcDevice : public DeviceAdapter
{
public:
    virtual status_t initParameters(CameraParameters& params, int* supportRecordingFormat, int rfmtLen,
                        int* supportPictureFormat, int pfmtLen) {return 0;}
    virtual status_t setParameters(CameraParameters& params) {return 0;}

private:
    PixelFormat getMatchFormat(int* sfmt, int slen, int* dfmt, int dlen) {return 0;}
    status_t setSupportedPreviewFormats(int* sfmt, int slen, int* dfmt, int dlen) {return 0;}
    status_t setPreviewStringFormat(PixelFormat format) {return 0;}

private:
    char mSupportedFPS[MAX_SENSOR_FORMAT];
    char mSupportedPictureSizes[CAMER_PARAM_BUFFER_SIZE];
    char mSupportedPreviewSizes[CAMER_PARAM_BUFFER_SIZE];
};

#endif

