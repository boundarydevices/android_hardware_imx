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

#ifndef _JPEG_BUILDER_H_
#define _JPEG_BUILDER_H_

#include "CameraUtil.h"
#include <utils/RefBase.h>
#include "YuvToJpegEncoder.h"

extern "C" {
#include "jhead.h"
}

namespace android {

#define EXIF_MAKENOTE "fsl_makernote"
#define EXIF_MODEL    "fsl_model"

#define MAX_EXIF_TAGS_SUPPORTED 30

static const char TAG_MODEL[] = "Model";
static const char TAG_MAKE[] = "Make";
static const char TAG_FOCALLENGTH[] = "FocalLength";
static const char TAG_DATETIME[] = "DateTime";
static const char TAG_IMAGE_WIDTH[] = "ImageWidth";
static const char TAG_IMAGE_LENGTH[] = "ImageLength";
static const char TAG_GPS_LAT[] = "GPSLatitude";
static const char TAG_GPS_LAT_REF[] = "GPSLatitudeRef";
static const char TAG_GPS_LONG[] = "GPSLongitude";
static const char TAG_GPS_LONG_REF[] = "GPSLongitudeRef";
static const char TAG_GPS_ALT[] = "GPSAltitude";
static const char TAG_GPS_ALT_REF[] = "GPSAltitudeRef";
static const char TAG_GPS_MAP_DATUM[] = "GPSMapDatum";
static const char TAG_GPS_PROCESSING_METHOD[] = "GPSProcessingMethod";
static const char TAG_GPS_VERSION_ID[] = "GPSVersionID";
static const char TAG_GPS_TIMESTAMP[] = "GPSTimeStamp";
static const char TAG_GPS_DATESTAMP[] = "GPSDateStamp";
static const char TAG_ORIENTATION[] = "Orientation";

#define GPS_MIN_DIV                 60
#define GPS_SEC_DIV                 60
#define GPS_SEC_ACCURACY            1000

#define GPS_NORTH_REF               "N"
#define GPS_SOUTH_REF               "S"
#define GPS_EAST_REF                "E"
#define GPS_WEST_REF                "W"

#define EXIF_DATE_TIME_SIZE         20

#define GPS_DATESTAMP_SIZE          11
#define GPS_REF_SIZE                2
#define GPS_MAPDATUM_SIZE           100
#define GPS_PROCESSING_SIZE         100
#define GPS_VERSION_SIZE            4

struct GPSData
{
    int mLongDeg, mLongMin, mLongSec, mLongSecDiv;
    char mLongRef[GPS_REF_SIZE];
    bool mLongValid;
    int mLatDeg, mLatMin, mLatSec, mLatSecDiv;
    char mLatRef[GPS_REF_SIZE];
    bool mLatValid;
    int mAltitude;
    unsigned char mAltitudeRef;
    bool mAltitudeValid;
    char mMapDatum[GPS_MAPDATUM_SIZE];
    bool mMapDatumValid;
    char mVersionId[GPS_VERSION_SIZE];
    bool mVersionIdValid;
    char mProcMethod[GPS_PROCESSING_SIZE];
    bool mProcMethodValid;
    char mDatestamp[GPS_DATESTAMP_SIZE];
    bool mDatestampValid;
    uint32_t mTimeStampHour;
    uint32_t mTimeStampMin;
    uint32_t mTimeStampSec;
    bool mTimeStampValid;
};

struct EXIFData
{
    GPSData mGPSData;
    bool mMakeValid;
    bool mModelValid;
};

struct JpegParams {
    JpegParams(uint8_t* uSrc, int srcSize, uint8_t* uDst, int dstSize,
            int quality, int inWidth, int inHeight, int outWidth,
            int outHeight, const char* format)
        : src(uSrc), src_size(srcSize), dst(uDst), dst_size(dstSize),
          quality(quality), in_width(inWidth), in_height(inHeight),
          out_width(outWidth), out_height(outHeight), format(format),
          jpeg_size(0)
    {}

    uint8_t* src;
    int src_size;
    uint8_t* dst;
    int dst_size;
    int quality;
    int in_width;
    int in_height;
    int out_width;
    int out_height;
    const char* format;
    size_t jpeg_size;
 };


class JpegBuilder : public LightRefBase<JpegBuilder>
{
public:
    JpegBuilder();
    ~JpegBuilder();

    status_t getSupportedPictureFormat(int* pFormat, int len);
    void prepareImage(const CameraParameters& params);
    void setParameters(const CameraParameters &params);

    status_t encodeImage(JpegParams* mainJpeg, JpegParams* thumbNail);
    size_t getImageSize();
    status_t buildImage(camera_request_memory get_memory, camera_memory_t** image);
    void reset();

private:
    status_t insertElement(const char* tag, const char* value);
    void insertExifToJpeg(unsigned char* jpeg, size_t jpeg_size);
    status_t insertExifThumbnailImage(const char*, int);
    void saveJpeg(unsigned char* picture, size_t jpeg_size);

private:
    status_t encodeJpeg(JpegParams* input);
    const char* degreesToExifOrientation(const char*);
    void stringToRational(const char*, unsigned int*, unsigned int*);
    bool isAsciiTag(const char* tag);
    status_t convertGPSCoord(double coord, int &deg, int &min, int &sec, int &secDivisor);

private:
    JpegParams* mMainInput;
    JpegParams* mThumbnailInput;

    bool mCancelEncoding;
    CameraFrame::FrameType mType;
    EXIFData mEXIFData;

private:
    ExifElement_t table[MAX_EXIF_TAGS_SUPPORTED];
    unsigned int gps_tag_count;
    unsigned int exif_tag_count;
    unsigned int position;
    bool jpeg_opened;
    bool has_datetime_tag;
};

};

#endif
