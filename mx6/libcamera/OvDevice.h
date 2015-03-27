/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2012-2015 Freescale Semiconductor, Inc.
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

#ifndef _OV_DEVICE_H_
#define _OV_DEVICE_H_

#include "CameraUtil.h"
#include "DeviceAdapter.h"

class OvDevice : public DeviceAdapter {
public:
    virtual status_t initParameters(CameraParameters& params,
                                    int              *supportRecordingFormat,
                                    int               rfmtLen,
                                    int              *supportPictureFormat,
                                    int               pfmtLen);
    virtual status_t setParameters(CameraParameters& params);
    virtual int getCaptureMode(int width,
                          int height);

protected:
    PixelFormat      getMatchFormat(int *sfmt,
                                    int  slen,
                                    int *dfmt,
                                    int  dlen);
    status_t setSupportedPreviewFormats(int *sfmt,
                                        int  slen,
                                        int *dfmt,
                                        int  dlen);
    status_t setPreviewStringFormat(PixelFormat format);

protected:
    char mSupportedFPS[MAX_SENSOR_FORMAT];
    char mSupportedPictureSizes[CAMER_PARAM_BUFFER_SIZE+1];
    char mSupportedPreviewSizes[CAMER_PARAM_BUFFER_SIZE+1];
};

#endif // ifndef _OV_DEVICE_H_
