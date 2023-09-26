/*
 * Copyright (C) 2012-2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2020 NXP
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

#include <utils/Errors.h>
#include <utils/RefBase.h>

#include "CameraMetadata.h"
#include "CameraUtils.h"
#include "ExifUtils.h"
#include "HwJpegEncoder.h"
#include "YuvToJpegEncoder.h"

namespace android {
#define EXIF_MAKENOTE "fsl_makernote"
#define EXIF_MODEL "fsl_model"

// static const char TAG_GPS_PROCESSING_METHOD[] = "GPSProcessingMethod";
/*static const char TAG_GPS_LAT[]               = "GPSLatitude";
static const char TAG_GPS_LAT_REF[]           = "GPSLatitudeRef";
static const char TAG_GPS_LONG[]              = "GPSLongitude";
static const char TAG_GPS_LONG_REF[]          = "GPSLongitudeRef";
static const char TAG_GPS_ALT[]               = "GPSAltitude";
static const char TAG_GPS_ALT_REF[]           = "GPSAltitudeRef";
static const char TAG_GPS_MAP_DATUM[]         = "GPSMapDatum";
static const char TAG_GPS_PROCESSING_METHOD[] = "GPSProcessingMethod";
static const char TAG_GPS_VERSION_ID[]        = "GPSVersionID";
static const char TAG_GPS_TIMESTAMP[]         = "GPSTimeStamp";
static const char TAG_GPS_DATESTAMP[]         = "GPSDateStamp";**/

#define MAX_EXIF_TAGS_SUPPORTED 30
#define GPS_MIN_DIV 60
#define GPS_SEC_DIV 60
#define GPS_SEC_ACCURACY 1000

#define GPS_NORTH_REF "N"
#define GPS_SOUTH_REF "S"
#define GPS_EAST_REF "E"
#define GPS_WEST_REF "W"

#define EXIF_DATE_TIME_SIZE 20

#define GPS_DATESTAMP_SIZE 11
#define GPS_REF_SIZE 2
#define GPS_MAPDATUM_SIZE 100
#define GPS_PROCESSING_SIZE 100
#define GPS_VERSION_SIZE 4

struct GPSData {
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

struct EXIFData {
    GPSData mGPSData;
    bool mMakeValid;
    bool mModelValid;
};

struct JpegParams {
    JpegParams(uint8_t *uSrc, uint8_t *uSrcPhy, int srcSize, int srcFd, buffer_handle_t srcHandle,
               uint8_t *uDst, int dstSize, int quality, int inWidth, int inHeight, int outWidth,
               int outHeight, int format)
          : src(uSrc),
            srcPhy(uSrcPhy),
            src_size(srcSize),
            src_fd(srcFd),
            src_handle(srcHandle),
            dst(uDst),
            dst_size(dstSize),
            quality(quality),
            in_width(inWidth),
            in_height(inHeight),
            out_width(outWidth),
            out_height(outHeight),
            format(format),
            jpeg_size(0) {}

    uint8_t *src;
    uint8_t *srcPhy;
    int src_size;
    int src_fd;
    buffer_handle_t src_handle;
    uint8_t *dst;
    int dst_size;
    int quality;
    int in_width;
    int in_height;
    int out_width;
    int out_height;
    int format;
    size_t jpeg_size;
};

class JpegBuilder : public LightRefBase<JpegBuilder> {
public:
    JpegBuilder();
    ~JpegBuilder();

    status_t encodeImage(JpegParams *mainJpeg, JpegParams *thumbNail, char *hw_jpeg_enc,
                         CameraMetadata &meta);
    size_t getImageSize() { return mRequestSize; }
    status_t buildImage(ImxStreamBuffer *streamBuf, char *hw_jpeg_enc);
    void reset();
    void setMetadata(CameraMetadata *pMeta);

private:
    status_t encodeJpeg(JpegParams *input, char *hw_jpeg_enc, const void *app1Buffer,
                        size_t app1Size);
    const char *degreesToExifOrientation(const char *);
    void stringToRational(const char *, unsigned int *, unsigned int *);
    bool isAsciiTag(const char *tag);
    status_t convertGPSCoord(double coord, int &deg, int &min, int &sec, int &secDivisor);

    int InsertEXIFAndJpeg(uint8_t *pMain, uint32_t mainSize, uint8_t *pDst, uint32_t dstSize);

private:
    JpegParams *mMainInput;
    JpegParams *mThumbnailInput;

    size_t exifDataSize;
    const uint8_t *exifData;

    bool mCancelEncoding;
    EXIFData mEXIFData;
    ExifUtils *mExifUtils;

private:
    bool has_datetime_tag;

    CameraMetadata *mMeta;
    uint32_t mRequestSize;
};
}; // namespace android

#endif // ifndef _JPEG_BUILDER_H_
