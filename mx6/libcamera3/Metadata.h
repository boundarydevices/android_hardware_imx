/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
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

#ifndef _METADATA_H_
#define _METADATA_H_

#include <stdint.h>
#include <hardware/camera3.h>
#include <system/camera_metadata.h>
#include <camera/CameraMetadata.h>
#include "CameraUtils.h"

// Metadata is a convenience class for dealing with libcamera_metadata
class Metadata : public LightRefBase<Metadata>
{
public:
    Metadata() {}
    Metadata(const camera_metadata_t *metadata);
    ~Metadata();

    static camera_metadata_t* createStaticInfo(SensorData& sensor);
    static void createSettingTemplate(Metadata& base, SensorData& sensor,
                                      int request_template);

    camera_metadata_entry_t find(uint32_t tag);
    //void clear();
    int32_t getRequestType();

    int32_t getGpsCoordinates(double *pCoords, int count);
    int32_t getGpsTimeStamp(int64_t &timeStamp);
    int32_t getGpsProcessingMethod(uint8_t* src, int count);
    int32_t getFocalLength(float &focalLength);
    int32_t getJpegRotation(int32_t &jpegRotation);
    int32_t getJpegQuality(int32_t &quality);
    int32_t getJpegThumbQuality(int32_t &thumb);
    int32_t getJpegThumbSize(int &width, int &height);

    // Initialize with framework metadata
    //int init(const camera_metadata_t *metadata);

    // Parse and add an entry. Allocates and copies new storage for *data.
    int addUInt8(uint32_t tag, int count, const uint8_t *data);
    int add1UInt8(uint32_t tag, const uint8_t data);
    int addInt32(uint32_t tag, int count, const int32_t *data);
    int addFloat(uint32_t tag, int count, const float *data);
    int addInt64(uint32_t tag, int count, const int64_t *data);
    int addDouble(uint32_t tag, int count, const double *data);
    int addRational(uint32_t tag, int count,
            const camera_metadata_rational_t *data);

    /**
     * Is the buffer empty (no entires)
     */
    bool isEmpty() const;
    // Get a handle to the current metadata
    // This is not a durable handle, and may be destroyed by add*/init
    camera_metadata_t* get();

private:
    // Actual internal storage
    CameraMetadata mData;
};

#endif // METADATA_H_
