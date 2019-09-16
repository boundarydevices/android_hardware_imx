/*
 * Copyright (C) 2012-2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2019 NXP
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

#include "CameraUtils.h"
#include <utils/RefBase.h>
#include "TinyExif.h"
#include "YuvToJpegEncoder.h"

namespace android {
#define EXIF_MAKENOTE "fsl_makernote"
#define EXIF_MODEL    "fsl_model"

//static const char TAG_GPS_PROCESSING_METHOD[] = "GPSProcessingMethod";
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
    int           mLongDeg, mLongMin, mLongSec, mLongSecDiv;
    char          mLongRef[GPS_REF_SIZE];
    bool          mLongValid;
    int           mLatDeg, mLatMin, mLatSec, mLatSecDiv;
    char          mLatRef[GPS_REF_SIZE];
    bool          mLatValid;
    int           mAltitude;
    unsigned char mAltitudeRef;
    bool          mAltitudeValid;
    char          mMapDatum[GPS_MAPDATUM_SIZE];
    bool          mMapDatumValid;
    char          mVersionId[GPS_VERSION_SIZE];
    bool          mVersionIdValid;
    char          mProcMethod[GPS_PROCESSING_SIZE];
    bool          mProcMethodValid;
    char          mDatestamp[GPS_DATESTAMP_SIZE];
    bool          mDatestampValid;
    uint32_t      mTimeStampHour;
    uint32_t      mTimeStampMin;
    uint32_t      mTimeStampSec;
    bool          mTimeStampValid;
};

struct EXIFData
{
    GPSData mGPSData;
    bool    mMakeValid;
    bool    mModelValid;
};

struct JpegParams {
    JpegParams(uint8_t *uSrc,
		  uint8_t*uSrcPhy,
               int     srcSize,
               uint8_t *uDst,
               int     dstSize,
               int     quality,
               int     inWidth,
               int     inHeight,
               int     outWidth,
               int     outHeight,
               int     format)
        : src(uSrc), srcPhy(uSrcPhy),src_size(srcSize), dst(uDst), dst_size(dstSize),
          quality(quality), in_width(inWidth), in_height(inHeight),
          out_width(outWidth), out_height(outHeight), format(format),
          jpeg_size(0)
    {}

    uint8_t    *src;
    uint8_t    *srcPhy;
    int         src_size;
    uint8_t    *dst;
    int         dst_size;
    int         quality;
    int         in_width;
    int         in_height;
    int         out_width;
    int         out_height;
    int         format;
    size_t      jpeg_size;
};


class JpegBuilder : public LightRefBase<JpegBuilder> {
public:
    JpegBuilder();
    ~JpegBuilder();

    status_t prepareImage(const StreamBuffer *streamBuf);

    status_t encodeImage(JpegParams *mainJpeg,
                         JpegParams *thumbNail);
    size_t   getImageSize() { return mRequestSize; }
    status_t buildImage(StreamBuffer *streamBuf);
    void     reset();
    void setMetadata(sp<Metadata> meta);

private:
    status_t insertElement(uint16_t tag,
                           uint32_t val1,
                           uint32_t val2,
                           uint32_t val3,
                           uint32_t val4,
                           uint32_t val5,
                           uint32_t val6,
                           char *strVal);
    void     insertExifToJpeg(unsigned char *jpeg,
                              size_t         jpeg_size);
    status_t insertExifThumbnailImage(const char *,
                                      int);
    void     saveJpeg(unsigned char *picture,
                      size_t         jpeg_size);

private:
    status_t    encodeJpeg(JpegParams *input);
    const char* degreesToExifOrientation(const char *);
    void        stringToRational(const    char *,
                                 unsigned int *,
                                 unsigned int *);
    bool        isAsciiTag(const char *tag);
    status_t    convertGPSCoord(double coord,
                                int  & deg,
                                int  & min,
                                int  & sec,
                                int  & secDivisor);

private:
    JpegParams *mMainInput;
    JpegParams *mThumbnailInput;

    bool mCancelEncoding;
    EXIFData mEXIFData;

private:
    IFDEle table[MAX_EXIF_TAGS_SUPPORTED];
    unsigned int  gps_tag_count;
    unsigned int  position;
    bool jpeg_opened;
    bool has_datetime_tag;

    sp<Metadata> mMeta;
    uint32_t mRequestSize;
};
};

#endif // ifndef _JPEG_BUILDER_H_
