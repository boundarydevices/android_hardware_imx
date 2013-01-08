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

#ifndef _METADA_MANAGER_H_
#define _METADA_MANAGER_H_

#include "CameraUtil.h"

#define MAX_VPU_SUPPORT_FORMAT 2
#define MAX_PICTURE_SUPPORT_FORMAT 2

using namespace android;

struct SensorInfo;

class MetadaManager : public LightRefBase<MetadaManager>
{
public:
    MetadaManager(SensorInfo *dev, int cameraId);
    ~MetadaManager();

    status_t addOrSize(camera_metadata_t *request,
        bool sizeRequest,
        size_t *entryCount,
        size_t *dataCount,
        uint32_t tag,
        const void *entryData,
        size_t entryDataCount);

    status_t createStaticInfo(camera_metadata_t **info, bool sizeRequest);

    status_t createDefaultRequest(
        int request_template,
        camera_metadata_t **request,
        bool sizeRequest);

    status_t setCurrentRequest(camera_metadata_t* request);
    status_t getRequestType(int *reqType);
    status_t getRequestStreams(camera_metadata_entry_t *reqStreams);
    status_t getFrameRate(int *value);

    status_t getGpsCoordinates(double *pCoords, int count);
    status_t getGpsTimeStamp(int64_t &timeStamp);
    status_t getGpsProcessingMethod(uint8_t* src, int count);
    status_t getFocalLength(float &focalLength);
    status_t getJpegRotation(int32_t &jpegRotation);
    status_t getJpegQuality(int32_t &quality);
    status_t getJpegThumbQuality(int32_t &thumb);
    status_t getJpegThumbSize(int &width, int &height);

    status_t getSupportedRecordingFormat(int *src, int len);
    status_t getSupportedPictureFormat(int *src, int len);

private:
    camera_metadata_t* mCurrentRequest;
    SensorInfo *mSensorInfo;

    int mVpuSupportFmt[MAX_VPU_SUPPORT_FORMAT];
    int mPictureSupportFmt[MAX_PICTURE_SUPPORT_FORMAT];
    int mCameraId;
};

#endif
