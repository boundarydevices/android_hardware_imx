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

#include <system/camera_metadata.h>

//#define LOG_NDEBUG 0
#include <cutils/log.h>
#include "Metadata.h"

Metadata::Metadata(const camera_metadata_t *metadata)
{
    mData = metadata;
}

Metadata::~Metadata()
{
    mData.clear();
}

camera_metadata_entry_t Metadata::find(uint32_t tag)
{
    return mData.find(tag);
}

int32_t Metadata::getRequestType()
{
    camera_metadata_entry_t intent =
            mData.find(ANDROID_CONTROL_CAPTURE_INTENT);
    if (intent.count <= 0) {
        return 0;
    }

    if (intent.data.u8[0] == ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE) {
        return TYPE_STILLCAP;
    }
    else if (intent.data.u8[0] == ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT) {
        return TYPE_SNAPSHOT;
    }

    return TYPE_PREVIEW;
}

int32_t Metadata::getGpsCoordinates(double *pCoords, int count)
{
    camera_metadata_entry_t entry;
    entry = mData.find(ANDROID_JPEG_GPS_COORDINATES);
    if (entry.count == 0) {
        ALOGE("%s: error reading jpeg Coordinates tag", __FUNCTION__);
        return BAD_VALUE;
    }

    for (int i=0; i<(int)entry.count && i<count; i++) {
        pCoords[i] = entry.data.d[i];
    }

    return NO_ERROR;
}

int32_t Metadata::getGpsTimeStamp(int64_t &timeStamp)
{
    camera_metadata_entry_t entry;
    entry = mData.find(ANDROID_JPEG_GPS_TIMESTAMP);
    if (entry.count == 0) {
        ALOGE("%s: error reading jpeg TimeStamp tag", __FUNCTION__);
        return BAD_VALUE;
    }

    timeStamp = entry.data.i64[0];
    return NO_ERROR;
}

int32_t Metadata::getGpsProcessingMethod(uint8_t* src, int count)
{
    camera_metadata_entry_t entry;
    entry = mData.find(ANDROID_JPEG_GPS_PROCESSING_METHOD);
    if (entry.count == 0) {
        ALOGE("%s: error reading jpeg ProcessingMethod tag", __FUNCTION__);
        return BAD_VALUE;
    }

    int i;
    for (i=0; i<(int)entry.count && i<count-1; i++) {
        src[i] = entry.data.u8[i];
    }
    src[i] = '\0';

    return NO_ERROR;
}

int32_t Metadata::getJpegRotation(int32_t &jpegRotation)
{
    camera_metadata_entry_t entry;
    entry = mData.find(ANDROID_JPEG_ORIENTATION);
    if (entry.count == 0) {
        ALOGE("%s: error reading jpeg Rotation tag", __FUNCTION__);
        return BAD_VALUE;
    }

    jpegRotation = entry.data.i32[0];
    return NO_ERROR;
}

int32_t Metadata::getJpegQuality(int32_t &quality)
{
    uint8_t u8Quality = 0;
    camera_metadata_entry_t entry;
    entry = mData.find(ANDROID_JPEG_QUALITY);
    if (entry.count == 0) {
        ALOGE("%s: error reading jpeg quality tag", __FUNCTION__);
        return BAD_VALUE;
    }

    //4.3 framework change quality type from i32 to u8
    u8Quality = entry.data.u8[0];
    quality = u8Quality;

    return NO_ERROR;
}

int32_t Metadata::getJpegThumbQuality(int32_t &thumb)
{
    uint8_t u8Quality = 0;
    camera_metadata_entry_t entry;
    entry = mData.find(ANDROID_JPEG_THUMBNAIL_QUALITY);
    if (entry.count == 0) {
        ALOGE("%s: error reading jpeg thumbnail quality tag", __FUNCTION__);
        return BAD_VALUE;
    }

    //4.3 framework change quality type from i32 to u8
    u8Quality = entry.data.u8[0];
    thumb = u8Quality;

    return NO_ERROR;
}

int32_t Metadata::getJpegThumbSize(int &width, int &height)
{
    camera_metadata_entry_t entry;
    entry = mData.find(ANDROID_JPEG_THUMBNAIL_SIZE);
    if (entry.count == 0) {
        ALOGE("%s: error reading jpeg thumbnail size tag", __FUNCTION__);
        return BAD_VALUE;
    }

    width = entry.data.i32[0];
    height = entry.data.i32[1];
    return NO_ERROR;
}

int32_t Metadata::getFocalLength(float &focalLength)
{
    camera_metadata_entry_t entry;
    entry = mData.find(ANDROID_LENS_FOCAL_LENGTH);
    if (entry.count == 0) {
        ALOGE("%s: error reading lens focal length size tag", __FUNCTION__);
        return BAD_VALUE;
    }

    focalLength = entry.data.f[0];

    return 0;
}
#if 0
void Metadata::clear()
{
    mData.clear();
}
#endif
camera_metadata_t* Metadata::createStaticInfo(SensorData& sensor)
{
    /*
     * Setup static camera info.  This will have to customized per camera
     * device.
     */
    Metadata m;

    /* android.control */
    m.addInt32(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
            ARRAY_SIZE(sensor.mTargetFpsRange),
            sensor.mTargetFpsRange);

    int32_t android_control_ae_compensation_range[] = {-3, 3};
    m.addInt32(ANDROID_CONTROL_AE_COMPENSATION_RANGE,
            ARRAY_SIZE(android_control_ae_compensation_range),
            android_control_ae_compensation_range);

    camera_metadata_rational_t android_control_ae_compensation_step[] = {{1,1}};
    m.addRational(ANDROID_CONTROL_AE_COMPENSATION_STEP,
            ARRAY_SIZE(android_control_ae_compensation_step),
            android_control_ae_compensation_step);

    int32_t android_control_max_regions[] = {/*AE*/ 1,/*AWB*/ 1,/*AF*/ 1};
    m.addInt32(ANDROID_CONTROL_MAX_REGIONS,
            ARRAY_SIZE(android_control_max_regions),
            android_control_max_regions);

    /* android.jpeg */
    int32_t android_jpeg_available_thumbnail_sizes[] = {96, 96, 160, 120, 0, 0};
    m.addInt32(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
            ARRAY_SIZE(android_jpeg_available_thumbnail_sizes),
            android_jpeg_available_thumbnail_sizes);

    int32_t android_jpeg_max_size[] = {8 * 1024 * 1024}; // 8MB
    m.addInt32(ANDROID_JPEG_MAX_SIZE,
            ARRAY_SIZE(android_jpeg_max_size),
            android_jpeg_max_size);

    /* android.lens */
    float android_lens_info_available_focal_lengths[] = {sensor.mFocalLength};
    m.addFloat(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
            ARRAY_SIZE(android_lens_info_available_focal_lengths),
            android_lens_info_available_focal_lengths);
#if 0
    /* android.request */
    int32_t android_request_max_num_output_streams[] = {0, 3, 1};
    m.addInt32(ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS,
            ARRAY_SIZE(android_request_max_num_output_streams),
            android_request_max_num_output_streams);
#endif
    /* android.scaler */
    m.addInt32(ANDROID_SCALER_AVAILABLE_FORMATS,
            sensor.mAvailableFormatCount,
            sensor.mAvailableFormats);

    int64_t android_scaler_available_jpeg_min_durations[] = {sensor.mMinFrameDuration};
    m.addInt64(ANDROID_SCALER_AVAILABLE_JPEG_MIN_DURATIONS,
            ARRAY_SIZE(android_scaler_available_jpeg_min_durations),
            android_scaler_available_jpeg_min_durations);

    m.addInt32(ANDROID_SCALER_AVAILABLE_JPEG_SIZES,
            sensor.mPictureResolutionCount,
            sensor.mPictureResolutions);

    float android_scaler_available_max_digital_zoom[] = {4};
    m.addFloat(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
            ARRAY_SIZE(android_scaler_available_max_digital_zoom),
            android_scaler_available_max_digital_zoom);

    int64_t android_scaler_available_processed_min_durations[] = {sensor.mMinFrameDuration};
    m.addInt64(ANDROID_SCALER_AVAILABLE_PROCESSED_MIN_DURATIONS,
            ARRAY_SIZE(android_scaler_available_processed_min_durations),
            android_scaler_available_processed_min_durations);

    m.addInt32(ANDROID_SCALER_AVAILABLE_PROCESSED_SIZES,
            sensor.mPreviewResolutionCount,
            sensor.mPreviewResolutions);

    int64_t android_scaler_available_raw_min_durations[] = {sensor.mMinFrameDuration};
    m.addInt64(ANDROID_SCALER_AVAILABLE_RAW_MIN_DURATIONS,
            ARRAY_SIZE(android_scaler_available_raw_min_durations),
            android_scaler_available_raw_min_durations);

    int32_t android_scaler_available_raw_sizes[] = {sensor.mMaxWidth, sensor.mMaxHeight};
    m.addInt32(ANDROID_SCALER_AVAILABLE_RAW_SIZES,
            ARRAY_SIZE(android_scaler_available_raw_sizes),
            android_scaler_available_raw_sizes);

    /* android.sensor */

    int32_t android_sensor_info_active_array_size[] = {sensor.mMaxWidth, sensor.mMaxHeight};
    m.addInt32(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
            ARRAY_SIZE(android_sensor_info_active_array_size),
            android_sensor_info_active_array_size);
#if 0
    int32_t android_sensor_info_sensitivity_range[] =
            {100, 1600};
    m.addInt32(ANDROID_SENSOR_INFO_SENSITIVITY_RANGE,
            ARRAY_SIZE(android_sensor_info_sensitivity_range),
            android_sensor_info_sensitivity_range);
#endif
    int64_t android_sensor_info_max_frame_duration[] = {sensor.mMaxFrameDuration};
    m.addInt64(ANDROID_SENSOR_INFO_MAX_FRAME_DURATION,
            ARRAY_SIZE(android_sensor_info_max_frame_duration),
            android_sensor_info_max_frame_duration);

    float android_sensor_info_physical_size[] = {sensor.mPhysicalWidth, sensor.mPhysicalHeight};
    m.addFloat(ANDROID_SENSOR_INFO_PHYSICAL_SIZE,
            ARRAY_SIZE(android_sensor_info_physical_size),
            android_sensor_info_physical_size);

    int32_t android_sensor_info_pixel_array_size[] = {sensor.mMaxWidth, sensor.mMaxHeight};
    m.addInt32(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
            ARRAY_SIZE(android_sensor_info_pixel_array_size),
            android_sensor_info_pixel_array_size);
#if 0
    int32_t android_sensor_orientation[] = {0};
    m.addInt32(ANDROID_SENSOR_ORIENTATION,
            ARRAY_SIZE(android_sensor_orientation),
            android_sensor_orientation);
#endif
    /* End of static camera characteristics */

    return clone_camera_metadata(m.get());
}

void Metadata::createSettingTemplate(Metadata& base, SensorData& sensor,
                                     int request_template)
{
    /** android.request */
    static const uint8_t metadataMode = ANDROID_REQUEST_METADATA_MODE_NONE;
    base.addUInt8(ANDROID_REQUEST_METADATA_MODE, 1, &metadataMode);

    static const int32_t id = 0;
    base.addInt32(ANDROID_REQUEST_ID, 1, &id);

    static const int32_t frameCount = 0;
    base.addInt32(ANDROID_REQUEST_FRAME_COUNT, 1, &frameCount);

    /** android.lens */
    static const float focusDistance = 0;
    base.addFloat(ANDROID_LENS_FOCUS_DISTANCE, 1, &focusDistance);

    static float aperture = 2.8;
    base.addFloat(ANDROID_LENS_APERTURE, 1, &aperture);
    base.addFloat(ANDROID_LENS_FOCAL_LENGTH, 1, &sensor.mFocalLength);

    static const float filterDensity = 0;
    base.addFloat(ANDROID_LENS_FILTER_DENSITY, 1, &filterDensity);

    static const uint8_t opticalStabilizationMode =
            ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
    base.addUInt8(ANDROID_LENS_OPTICAL_STABILIZATION_MODE,
            1, &opticalStabilizationMode);

    /** android.sensor */
    static const int64_t frameDuration = 33333333L; // 1/30 s
    base.addInt64(ANDROID_SENSOR_FRAME_DURATION, 1, &frameDuration);


    /** android.flash */
    static const uint8_t flashMode = ANDROID_FLASH_MODE_OFF;
    base.addUInt8(ANDROID_FLASH_MODE, 1, &flashMode);

    static const uint8_t flashPower = 10;
    base.addUInt8(ANDROID_FLASH_FIRING_POWER, 1, &flashPower);

    static const int64_t firingTime = 0;
    base.addInt64(ANDROID_FLASH_FIRING_TIME, 1, &firingTime);

    /** Processing block modes */
    uint8_t hotPixelMode = 0;
    uint8_t demosaicMode = 0;
    uint8_t noiseMode = 0;
    uint8_t shadingMode = 0;
    uint8_t colorMode = 0;
    uint8_t tonemapMode = 0;
    uint8_t edgeMode = 0;
    uint8_t vstabMode = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF;

    switch (request_template) {
      case CAMERA3_TEMPLATE_PREVIEW:
        break;
      case CAMERA3_TEMPLATE_STILL_CAPTURE:
        break;
      case CAMERA3_TEMPLATE_VIDEO_RECORD:
        vstabMode = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_ON;
        break;
      case CAMERA3_TEMPLATE_VIDEO_SNAPSHOT:
        vstabMode = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_ON;
        break;
      case CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG:
        hotPixelMode = ANDROID_HOT_PIXEL_MODE_HIGH_QUALITY;
        demosaicMode = ANDROID_DEMOSAIC_MODE_HIGH_QUALITY;
        noiseMode = ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY;
        shadingMode = ANDROID_SHADING_MODE_HIGH_QUALITY;
        colorMode = ANDROID_COLOR_CORRECTION_MODE_HIGH_QUALITY;
        tonemapMode = ANDROID_TONEMAP_MODE_HIGH_QUALITY;
        edgeMode = ANDROID_EDGE_MODE_HIGH_QUALITY;
        break;
      default:
        hotPixelMode = ANDROID_HOT_PIXEL_MODE_FAST;
        demosaicMode = ANDROID_DEMOSAIC_MODE_FAST;
        noiseMode = ANDROID_NOISE_REDUCTION_MODE_FAST;
        shadingMode = ANDROID_SHADING_MODE_FAST;
        colorMode = ANDROID_COLOR_CORRECTION_MODE_FAST;
        tonemapMode = ANDROID_TONEMAP_MODE_FAST;
        edgeMode = ANDROID_EDGE_MODE_FAST;
        break;
    }
    base.addUInt8(ANDROID_HOT_PIXEL_MODE, 1, &hotPixelMode);
    base.addUInt8(ANDROID_DEMOSAIC_MODE, 1, &demosaicMode);
    base.addUInt8(ANDROID_NOISE_REDUCTION_MODE, 1, &noiseMode);
    base.addUInt8(ANDROID_SHADING_MODE, 1, &shadingMode);
    base.addUInt8(ANDROID_COLOR_CORRECTION_MODE, 1, &colorMode);
    base.addUInt8(ANDROID_TONEMAP_MODE, 1, &tonemapMode);
    base.addUInt8(ANDROID_EDGE_MODE, 1, &edgeMode);
    base.addUInt8(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, 1, &vstabMode);

    /** android.noise */
    static const uint8_t noiseStrength = 5;
    base.addUInt8(ANDROID_NOISE_REDUCTION_STRENGTH, 1, &noiseStrength);

    /** android.color */
    static const float colorTransform[9] = {
        1.0f, 0.f, 0.f,
        0.f, 1.f, 0.f,
        0.f, 0.f, 1.f
    };
    base.addFloat(ANDROID_COLOR_CORRECTION_TRANSFORM, 9, colorTransform);

    /** android.tonemap */
    static const float tonemapCurve[4] = {
        0.f, 0.f,
        1.f, 1.f
    };
    base.addFloat(ANDROID_TONEMAP_CURVE_RED, 4, tonemapCurve); // sungjoong
    base.addFloat(ANDROID_TONEMAP_CURVE_GREEN, 4, tonemapCurve);
    base.addFloat(ANDROID_TONEMAP_CURVE_BLUE, 4, tonemapCurve);

    /** android.edge */
    static const uint8_t edgeStrength = 5;
    base.addUInt8(ANDROID_EDGE_STRENGTH, 1, &edgeStrength);

    /** android.scaler */
    int32_t cropRegion[3] = {
        0, 0, /*mSensorInfo->mMaxWidth*/
    };
    base.addInt32(ANDROID_SCALER_CROP_REGION, 3, cropRegion);

    /** android.jpeg */
    //4.3 framework change quality type from i32 to u8
    static const uint8_t jpegQuality = 100;
    base.addUInt8(ANDROID_JPEG_QUALITY, 1, &jpegQuality);

    static const int32_t thumbnailSize[2] = {
        160, 120
    };
    base.addInt32(ANDROID_JPEG_THUMBNAIL_SIZE, 2, thumbnailSize);

    //4.3 framework change quality type from i32 to u8
    static const uint8_t thumbnailQuality = 100;
    base.addUInt8(ANDROID_JPEG_THUMBNAIL_QUALITY, 1, &thumbnailQuality);

    static const double gpsCoordinates[3] = {
        0, 0, 0
    };
    base.addDouble(ANDROID_JPEG_GPS_COORDINATES, 3, gpsCoordinates);

    static const uint8_t gpsProcessingMethod[32] = "None";
    base.addUInt8(ANDROID_JPEG_GPS_PROCESSING_METHOD, 32, gpsProcessingMethod);

    static const int64_t gpsTimestamp = 0;
    base.addInt64(ANDROID_JPEG_GPS_TIMESTAMP, 1, &gpsTimestamp);

    static const int32_t jpegOrientation = 0;
    base.addInt32(ANDROID_JPEG_ORIENTATION, 1, &jpegOrientation);

    /** android.stats */

    static const uint8_t faceDetectMode = ANDROID_STATISTICS_FACE_DETECT_MODE_FULL;
    base.addUInt8(ANDROID_STATISTICS_FACE_DETECT_MODE, 1, &faceDetectMode);

    static const uint8_t histogramMode = ANDROID_STATISTICS_HISTOGRAM_MODE_OFF;
    base.addUInt8(ANDROID_STATISTICS_HISTOGRAM_MODE, 1, &histogramMode);

    static const uint8_t sharpnessMapMode = ANDROID_STATISTICS_HISTOGRAM_MODE_OFF;
    base.addUInt8(ANDROID_STATISTICS_SHARPNESS_MAP_MODE, 1, &sharpnessMapMode);

    /** android.control */

    uint8_t controlIntent = 0;
    switch (request_template) {
      case CAMERA3_TEMPLATE_PREVIEW:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
        break;
      case CAMERA3_TEMPLATE_STILL_CAPTURE:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE;
        break;
      case CAMERA3_TEMPLATE_VIDEO_RECORD:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD;
        break;
      case CAMERA3_TEMPLATE_VIDEO_SNAPSHOT:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT;
        break;
      case CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_ZERO_SHUTTER_LAG;
        break;
      default:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_CUSTOM;
        break;
    }
    base.addUInt8(ANDROID_CONTROL_CAPTURE_INTENT, 1, &controlIntent);

    static const uint8_t controlMode = ANDROID_CONTROL_MODE_AUTO;
    base.addUInt8(ANDROID_CONTROL_MODE, 1, &controlMode);

    static const uint8_t effectMode = ANDROID_CONTROL_EFFECT_MODE_OFF;
    base.addUInt8(ANDROID_CONTROL_EFFECT_MODE, 1, &effectMode);

    static const uint8_t sceneMode = ANDROID_CONTROL_SCENE_MODE_DISABLED;
    base.addUInt8(ANDROID_CONTROL_SCENE_MODE, 1, &sceneMode);

    static const uint8_t aeMode = ANDROID_CONTROL_AE_MODE_ON;
    base.addUInt8(ANDROID_CONTROL_AE_MODE, 1, &aeMode);

    int32_t controlRegions[5] = {
        0, 0, sensor.mMaxWidth, sensor.mMaxHeight, 1000
    };
    base.addInt32(ANDROID_CONTROL_AE_REGIONS, 5, controlRegions);

    static const int32_t aeExpCompensation = 0;
    base.addInt32(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, 1, &aeExpCompensation);

    static const int32_t aeTargetFpsRange[2] = {
        15, 30
    };
    base.addInt32(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, 2, aeTargetFpsRange);

    static const uint8_t aeAntibandingMode =
            ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
    base.addUInt8(ANDROID_CONTROL_AE_ANTIBANDING_MODE, 1, &aeAntibandingMode);

    static const uint8_t awbMode =
            ANDROID_CONTROL_AWB_MODE_AUTO;
    base.addUInt8(ANDROID_CONTROL_AWB_MODE, 1, &awbMode);

    base.addInt32(ANDROID_CONTROL_AWB_REGIONS, 5, controlRegions);

    uint8_t afMode = 0;
    switch (request_template) {
      case CAMERA3_TEMPLATE_PREVIEW:
        afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
        break;
      case CAMERA3_TEMPLATE_STILL_CAPTURE:
        afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
        break;
      case CAMERA3_TEMPLATE_VIDEO_RECORD:
        afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
        break;
      case CAMERA3_TEMPLATE_VIDEO_SNAPSHOT:
        afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
        break;
      case CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG:
        afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
        break;
      default:
        afMode = ANDROID_CONTROL_AF_MODE_AUTO;
        break;
    }
    base.addUInt8(ANDROID_CONTROL_AF_MODE, 1, &afMode);

    base.addInt32(ANDROID_CONTROL_AF_REGIONS, 5, controlRegions);
}
#if 0
int Metadata::init(const camera_metadata_t *metadata)
{
    mData = metadata;

    return 0;
}
#endif
int Metadata::addUInt8(uint32_t tag, int count, const uint8_t *data)
{
    return mData.update(tag, data, count);
}

int Metadata::add1UInt8(uint32_t tag, const uint8_t data)
{
    return addUInt8(tag, 1, &data);
}

int Metadata::addInt32(uint32_t tag, int count, const int32_t *data)
{
    return mData.update(tag, data, count);
}

int Metadata::addFloat(uint32_t tag, int count, const float *data)
{
    return mData.update(tag, data, count);
}

int Metadata::addInt64(uint32_t tag, int count, const int64_t *data)
{
    return mData.update(tag, data, count);
}

int Metadata::addDouble(uint32_t tag, int count, const double *data)
{
    return mData.update(tag, data, count);
}

int Metadata::addRational(uint32_t tag, int count,
        const camera_metadata_rational_t *data)
{
    return mData.update(tag, data, count);
}

bool Metadata::isEmpty() const {
    return mData.isEmpty();
}

camera_metadata_t* Metadata::get()
{
    const camera_metadata_t* data = mData.getAndLock();
    mData.unlock(data);

    return (camera_metadata_t*)data;
}

