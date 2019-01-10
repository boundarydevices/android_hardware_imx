/*
 * Copyright (C) 2015-2016 Freescale Semiconductor, Inc.
 * Copyright 2017 NXP
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

camera_metadata_t* Metadata::createStaticInfo(SensorData& sensor, camera_info &camInfo)
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

    static const uint8_t aeAntibandingMode =
            ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
    m.addUInt8(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES, 1, &aeAntibandingMode);

    int32_t android_control_ae_compensation_range[] = {-3, 3};
    m.addInt32(ANDROID_CONTROL_AE_COMPENSATION_RANGE,
            ARRAY_SIZE(android_control_ae_compensation_range),
            android_control_ae_compensation_range);

    camera_metadata_rational_t android_control_ae_compensation_step[] = {{1,1}};
    m.addRational(ANDROID_CONTROL_AE_COMPENSATION_STEP,
            ARRAY_SIZE(android_control_ae_compensation_step),
            android_control_ae_compensation_step);

    int32_t android_control_max_regions[] = {/*AE*/ 0, /*AWB*/ 0, /*AF*/ 0};
    m.addInt32(ANDROID_CONTROL_MAX_REGIONS,
            ARRAY_SIZE(android_control_max_regions),
            android_control_max_regions);

    /* android.jpeg */
    int32_t android_jpeg_available_thumbnail_sizes[] = {0, 0, 96, 96, 160, 90, 160, 120};
    m.addInt32(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
            ARRAY_SIZE(android_jpeg_available_thumbnail_sizes),
            android_jpeg_available_thumbnail_sizes);

    int32_t android_jpeg_max_size[] = {sensor.mMaxJpegSize}; // default 8MB
    m.addInt32(ANDROID_JPEG_MAX_SIZE,
            ARRAY_SIZE(android_jpeg_max_size),
            android_jpeg_max_size);

    /* android.lens */
    float android_lens_info_available_focal_lengths[] = {sensor.mFocalLength};
    m.addFloat(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
            ARRAY_SIZE(android_lens_info_available_focal_lengths),
            android_lens_info_available_focal_lengths);

    float android_lens_info_available_apertures[] = {2.8};
    m.addFloat(ANDROID_LENS_INFO_AVAILABLE_APERTURES,
            ARRAY_SIZE(android_lens_info_available_apertures),
            android_lens_info_available_apertures);

    float android_lens_info_available_filter_densities[] = {0.0};
    m.addFloat(ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES,
            ARRAY_SIZE(android_lens_info_available_filter_densities),
            android_lens_info_available_filter_densities);

    uint8_t android_lens_info_available_optical_stabilization[] = {
            ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF};
    m.addUInt8(ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
            ARRAY_SIZE(android_lens_info_available_optical_stabilization),
            android_lens_info_available_optical_stabilization);

    /* android.request */
    int32_t android_request_max_num_output_streams[] = {0, 3, 1};
    m.addInt32(ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS,
            ARRAY_SIZE(android_request_max_num_output_streams),
            android_request_max_num_output_streams);

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

    /* left, top, right, bottom */
    int32_t android_sensor_info_active_array_size[] = {0, 0, sensor.mActiveArrayWidth, sensor.mActiveArrayHeight};
    m.addInt32(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
            ARRAY_SIZE(android_sensor_info_active_array_size),
            android_sensor_info_active_array_size);

    int32_t android_sensor_info_sensitivity_range[] =
            {100, 1600};
    m.addInt32(ANDROID_SENSOR_INFO_SENSITIVITY_RANGE,
            ARRAY_SIZE(android_sensor_info_sensitivity_range),
            android_sensor_info_sensitivity_range);

    int64_t android_sensor_info_max_frame_duration[] = {sensor.mMaxFrameDuration};
    m.addInt64(ANDROID_SENSOR_INFO_MAX_FRAME_DURATION,
            ARRAY_SIZE(android_sensor_info_max_frame_duration),
            android_sensor_info_max_frame_duration);

    int64_t kExposureTimeRange[2] = {1000L, 300000000L};
    m.addInt64(ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE, ARRAY_SIZE(kExposureTimeRange), kExposureTimeRange);

    float android_sensor_info_physical_size[] = {sensor.mPhysicalWidth, sensor.mPhysicalHeight};
    m.addFloat(ANDROID_SENSOR_INFO_PHYSICAL_SIZE,
            ARRAY_SIZE(android_sensor_info_physical_size),
            android_sensor_info_physical_size);

    int32_t android_sensor_info_pixel_array_size[] = {sensor.mPixelArrayWidth, sensor.mPixelArrayHeight};
    m.addInt32(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
            ARRAY_SIZE(android_sensor_info_pixel_array_size),
            android_sensor_info_pixel_array_size);

    int32_t android_sensor_orientation[] = {0};
    m.addInt32(ANDROID_SENSOR_ORIENTATION,
            ARRAY_SIZE(android_sensor_orientation),
            android_sensor_orientation);

    /* End of static camera characteristics */

    uint8_t availableSceneModes[] = {ANDROID_CONTROL_SCENE_MODE_DISABLED};
    m.addUInt8(ANDROID_CONTROL_AVAILABLE_SCENE_MODES,
             ARRAY_SIZE(availableSceneModes),
             availableSceneModes);

    uint8_t available_capabilities[] = {ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE};
    m.addUInt8(ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
            ARRAY_SIZE(available_capabilities),
            available_capabilities);

    uint8_t lensFacing = (camInfo.facing == CAMERA_FACING_BACK) ? ANDROID_LENS_FACING_BACK : ANDROID_LENS_FACING_FRONT;
    m.addUInt8(ANDROID_LENS_FACING,
               1,
               &lensFacing);

    int ResIdx = 0;
    int ResCount = 0;
    int streamConfigIdx = 0;
    int32_t streamConfig[MAX_RESOLUTION_SIZE * 6]; // MAX_RESOLUTION_SIZE/2 * 2 * 6;
    int64_t minFrmDuration[MAX_RESOLUTION_SIZE * 6];
    int64_t stallDuration[MAX_RESOLUTION_SIZE * 6];

    // TODO: It's better to get those info in seperate camera, and get the accurate fps.
    ResCount = sensor.mPreviewResolutionCount/2;
    for(ResIdx = 0; ResIdx < ResCount; ResIdx++) {
        streamConfigIdx = ResIdx*4;

        streamConfig[streamConfigIdx] = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
        streamConfig[streamConfigIdx+1] = sensor.mPreviewResolutions[ResIdx*2];
        streamConfig[streamConfigIdx+2] = sensor.mPreviewResolutions[ResIdx*2+1];
        streamConfig[streamConfigIdx+3] = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;

        minFrmDuration[streamConfigIdx] = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
        minFrmDuration[streamConfigIdx+1] = sensor.mPreviewResolutions[ResIdx*2];
        minFrmDuration[streamConfigIdx+2] = sensor.mPreviewResolutions[ResIdx*2+1];
        minFrmDuration[streamConfigIdx+3] = 33333333; // ns

        stallDuration[streamConfigIdx] = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
        stallDuration[streamConfigIdx+1] = sensor.mPreviewResolutions[ResIdx*2];
        stallDuration[streamConfigIdx+2] = sensor.mPreviewResolutions[ResIdx*2+1];
        stallDuration[streamConfigIdx + 3] = 0; // ns
    }

    ResCount = sensor.mPictureResolutionCount/2;
    for(ResIdx = 0; ResIdx < ResCount; ResIdx++) {
        streamConfigIdx = sensor.mPreviewResolutionCount*2 + ResIdx*4;

        streamConfig[streamConfigIdx] = HAL_PIXEL_FORMAT_BLOB;
        streamConfig[streamConfigIdx+1] = sensor.mPictureResolutions[ResIdx*2];
        streamConfig[streamConfigIdx+2] = sensor.mPictureResolutions[ResIdx*2+1];
        streamConfig[streamConfigIdx+3] = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;

        minFrmDuration[streamConfigIdx] = HAL_PIXEL_FORMAT_BLOB;
        minFrmDuration[streamConfigIdx+1] = sensor.mPictureResolutions[ResIdx*2];
        minFrmDuration[streamConfigIdx+2] = sensor.mPictureResolutions[ResIdx*2+1];
        minFrmDuration[streamConfigIdx+3] = 33333333; // ns

        stallDuration[streamConfigIdx] = HAL_PIXEL_FORMAT_BLOB;
        stallDuration[streamConfigIdx+1] = sensor.mPictureResolutions[ResIdx*2];
        stallDuration[streamConfigIdx+2] = sensor.mPictureResolutions[ResIdx*2+1];
        stallDuration[streamConfigIdx+3] = 33333333; // ns
    }

    ResCount = sensor.mPreviewResolutionCount / 2;
    for (ResIdx = 0; ResIdx < ResCount; ResIdx++) {
         streamConfigIdx = sensor.mPictureResolutionCount * 2 + sensor.mPreviewResolutionCount * 2 + ResIdx * 4;

         streamConfig[streamConfigIdx] = HAL_PIXEL_FORMAT_YCBCR_420_888;
         streamConfig[streamConfigIdx + 1] = sensor.mPreviewResolutions[ResIdx * 2];
         streamConfig[streamConfigIdx + 2] = sensor.mPreviewResolutions[ResIdx * 2 + 1];
         streamConfig[streamConfigIdx + 3] = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;

         minFrmDuration[streamConfigIdx] = HAL_PIXEL_FORMAT_YCBCR_420_888;
         minFrmDuration[streamConfigIdx + 1] = sensor.mPreviewResolutions[ResIdx * 2];
         minFrmDuration[streamConfigIdx + 2] = sensor.mPreviewResolutions[ResIdx * 2 + 1];
         minFrmDuration[streamConfigIdx + 3] = 33333333; // ns

         stallDuration[streamConfigIdx] = HAL_PIXEL_FORMAT_YCBCR_420_888;
         stallDuration[streamConfigIdx + 1] = sensor.mPreviewResolutions[ResIdx * 2];
         stallDuration[streamConfigIdx + 2] = sensor.mPreviewResolutions[ResIdx * 2 + 1];
         stallDuration[streamConfigIdx + 3] = 0;
    }

    m.addInt32(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                    streamConfigIdx + 4,
                    streamConfig);

    m.addInt64(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
                    streamConfigIdx + 4,
                    minFrmDuration);

    m.addInt64(ANDROID_SCALER_AVAILABLE_STALL_DURATIONS,
                    streamConfigIdx + 4,
                    stallDuration);

    uint8_t supportedHwLvl =  ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LEGACY;
    m.addUInt8(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,
                    1,
                    &supportedHwLvl);

    static const int32_t maxLatency = ANDROID_SYNC_MAX_LATENCY_UNKNOWN;
    m.addInt32(ANDROID_SYNC_MAX_LATENCY, 1, &maxLatency);

    static const uint8_t croppingType = ANDROID_SCALER_CROPPING_TYPE_FREEFORM;
    m.addUInt8(ANDROID_SCALER_CROPPING_TYPE, 1, &croppingType);

    static const int32_t tonemapCurvePoints = 128;
    m.addInt32(ANDROID_TONEMAP_MAX_CURVE_POINTS, 1, &tonemapCurvePoints);

    static const uint8_t afTrigger = ANDROID_CONTROL_AF_TRIGGER_IDLE;
    m.addUInt8(ANDROID_CONTROL_AF_TRIGGER, 1, &afTrigger);

    static const uint8_t tonemapMode = ANDROID_TONEMAP_MODE_FAST;
    m.addUInt8(ANDROID_TONEMAP_MODE, 1, &tonemapMode);

    static const uint8_t pipelineMaxDepth = 3;
    m.addUInt8(ANDROID_REQUEST_PIPELINE_MAX_DEPTH, 1, &pipelineMaxDepth);

    static const int32_t partialResultCount = 1;
    m.addInt32(ANDROID_REQUEST_PARTIAL_RESULT_COUNT, 1, &partialResultCount);

    static const uint8_t timestampSource = ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME;
    m.addUInt8(ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE, 1, &timestampSource);

    static const int32_t maxFaceCount = 0;
    m.addInt32(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT, 1, &maxFaceCount);

#if ANDROID_SDK_VERSION >= 28
    static const uint8_t aeLockAvailable = ANDROID_CONTROL_AE_LOCK_AVAILABLE_FALSE;
    m.addUInt8(ANDROID_CONTROL_AE_LOCK_AVAILABLE, 1, &aeLockAvailable);

    static const uint8_t awbLockAvailable = ANDROID_CONTROL_AWB_LOCK_AVAILABLE_FALSE;
    m.addUInt8(ANDROID_CONTROL_AWB_LOCK_AVAILABLE, 1, &awbLockAvailable);

    static const uint8_t availableControlModes[] = {ANDROID_CONTROL_MODE_OFF, ANDROID_CONTROL_MODE_AUTO, ANDROID_CONTROL_MODE_USE_SCENE_MODE};
    m.addUInt8(ANDROID_CONTROL_AVAILABLE_MODES, ARRAY_SIZE(availableControlModes), availableControlModes);

    static const uint8_t availableHotPixelModes[] = {ANDROID_HOT_PIXEL_MODE_FAST, ANDROID_HOT_PIXEL_MODE_HIGH_QUALITY};
    m.addUInt8(ANDROID_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES, ARRAY_SIZE(availableHotPixelModes), availableHotPixelModes);

    static const int32_t session_keys[] = {ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, ANDROID_CONTROL_AE_TARGET_FPS_RANGE};
    m.addInt32(ANDROID_REQUEST_AVAILABLE_SESSION_KEYS, ARRAY_SIZE(session_keys), session_keys);
#endif

    int32_t availableResultKeys[] = {ANDROID_SENSOR_TIMESTAMP, ANDROID_FLASH_STATE};
    m.addInt32(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS, ARRAY_SIZE(availableResultKeys), availableResultKeys);

    static const uint8_t availableVstabModes[] = {ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF};
    m.addUInt8(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES, ARRAY_SIZE(availableVstabModes), availableVstabModes);

    static const uint8_t availableEffects[] = {ANDROID_CONTROL_EFFECT_MODE_OFF};
    m.addUInt8(ANDROID_CONTROL_AVAILABLE_EFFECTS, ARRAY_SIZE(availableEffects), availableEffects);

    static const int32_t availableTestPatternModes[] = {ANDROID_SENSOR_TEST_PATTERN_MODE_OFF};
    m.addInt32(ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES, ARRAY_SIZE(availableTestPatternModes), availableTestPatternModes);

    static const uint8_t availableEdgeModes[] = {ANDROID_EDGE_MODE_OFF, ANDROID_EDGE_MODE_FAST, ANDROID_EDGE_MODE_HIGH_QUALITY};
    m.addUInt8(ANDROID_EDGE_AVAILABLE_EDGE_MODES, ARRAY_SIZE(availableEdgeModes), availableEdgeModes);

    static const uint8_t availableToneMapModes[] = {ANDROID_TONEMAP_MODE_CONTRAST_CURVE, ANDROID_TONEMAP_MODE_FAST, ANDROID_TONEMAP_MODE_HIGH_QUALITY};
    m.addUInt8(ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES, ARRAY_SIZE(availableToneMapModes), availableToneMapModes);

    static const uint8_t availableNoiseReductionModes[] = {ANDROID_NOISE_REDUCTION_MODE_OFF, ANDROID_NOISE_REDUCTION_MODE_FAST, ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY};
    m.addUInt8(ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES, ARRAY_SIZE(availableNoiseReductionModes), availableNoiseReductionModes);

    static const uint8_t availableAeModes[] = {ANDROID_CONTROL_AE_MODE_OFF, ANDROID_CONTROL_AE_MODE_ON};
    m.addUInt8(ANDROID_CONTROL_AE_AVAILABLE_MODES, ARRAY_SIZE(availableAeModes), availableAeModes);

    static const uint8_t availableAfModes[] = {ANDROID_CONTROL_AF_MODE_OFF};
    m.addUInt8(ANDROID_CONTROL_AF_AVAILABLE_MODES, ARRAY_SIZE(availableAfModes), availableAfModes);

    static const uint8_t availableAwbModes[] = {
        ANDROID_CONTROL_AWB_MODE_OFF,
        ANDROID_CONTROL_AWB_MODE_AUTO,
        ANDROID_CONTROL_AWB_MODE_INCANDESCENT,
        ANDROID_CONTROL_AWB_MODE_FLUORESCENT,
        ANDROID_CONTROL_AWB_MODE_DAYLIGHT,
        ANDROID_CONTROL_AWB_MODE_SHADE};
    m.addUInt8(ANDROID_CONTROL_AWB_AVAILABLE_MODES, ARRAY_SIZE(availableAwbModes), availableAwbModes);

    static const uint8_t availableAberrationModes[] = {
        ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF,
        ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST,
        ANDROID_COLOR_CORRECTION_ABERRATION_MODE_HIGH_QUALITY};
    m.addUInt8(ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
               ARRAY_SIZE(availableAberrationModes),
               availableAberrationModes);

    /* flahs info */
    uint8_t flashInfoAvailable = ANDROID_FLASH_INFO_AVAILABLE_FALSE;
    m.addUInt8(ANDROID_FLASH_INFO_AVAILABLE, 1, &flashInfoAvailable);

    int32_t characteristics_keys_basic[] = {
        ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
        ANDROID_CONTROL_AE_AVAILABLE_MODES,
        ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
        ANDROID_CONTROL_AE_COMPENSATION_RANGE,
        ANDROID_CONTROL_AE_COMPENSATION_STEP,
        ANDROID_CONTROL_AF_AVAILABLE_MODES,
        ANDROID_CONTROL_AVAILABLE_EFFECTS,
        ANDROID_CONTROL_AVAILABLE_SCENE_MODES,
        ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
        ANDROID_CONTROL_AWB_AVAILABLE_MODES,
#if ANDROID_SDK_VERSION >= 28
        ANDROID_CONTROL_AE_LOCK_AVAILABLE,
        ANDROID_CONTROL_AWB_LOCK_AVAILABLE,
        ANDROID_CONTROL_AVAILABLE_MODES,
#endif
        ANDROID_FLASH_INFO_AVAILABLE,
        ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,
        ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
        ANDROID_SCALER_CROPPING_TYPE,
        ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
        ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
        ANDROID_LENS_FACING,
        ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
        ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
        ANDROID_REQUEST_PARTIAL_RESULT_COUNT,
        ANDROID_REQUEST_PIPELINE_MAX_DEPTH,
        ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE,
        ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES,
        ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
        ANDROID_SENSOR_INFO_PHYSICAL_SIZE,
        ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
        ANDROID_SENSOR_ORIENTATION,
        ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
        ANDROID_STATISTICS_INFO_MAX_FACE_COUNT,
        ANDROID_SYNC_MAX_LATENCY};
    m.addInt32(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
               ARRAY_SIZE(characteristics_keys_basic),
               characteristics_keys_basic);

    /* face detect mode */
    uint8_t availableFaceDetectModes[] = {ANDROID_STATISTICS_FACE_DETECT_MODE_OFF};
    m.addUInt8(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
                    ARRAY_SIZE(availableFaceDetectModes),
                    availableFaceDetectModes);
    int32_t availableRequestKeys[] = {ANDROID_COLOR_CORRECTION_MODE,
                                      ANDROID_COLOR_CORRECTION_TRANSFORM,
                                      ANDROID_CONTROL_AE_ANTIBANDING_MODE,
                                      ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
                                      ANDROID_CONTROL_AE_MODE,
                                      ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
                                      ANDROID_CONTROL_AF_MODE,
                                      ANDROID_CONTROL_AWB_MODE,
                                      ANDROID_CONTROL_CAPTURE_INTENT,
                                      ANDROID_CONTROL_MODE,
                                      ANDROID_CONTROL_SCENE_MODE,
                                      ANDROID_CONTROL_VIDEO_STABILIZATION_MODE,
                                      ANDROID_DEMOSAIC_MODE,
                                      ANDROID_FLASH_FIRING_POWER,
                                      ANDROID_FLASH_FIRING_TIME,
                                      ANDROID_FLASH_MODE,
                                      ANDROID_JPEG_GPS_COORDINATES,
                                      ANDROID_JPEG_GPS_PROCESSING_METHOD,
                                      ANDROID_JPEG_GPS_TIMESTAMP,
                                      ANDROID_JPEG_ORIENTATION,
                                      ANDROID_JPEG_QUALITY,
                                      ANDROID_JPEG_THUMBNAIL_QUALITY,
                                      ANDROID_JPEG_THUMBNAIL_SIZE,
                                      ANDROID_LENS_FOCAL_LENGTH,
                                      ANDROID_LENS_FOCUS_DISTANCE,
                                      ANDROID_LENS_INFO_AVAILABLE_APERTURES,
                                      ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES,
                                      ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
                                      ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                                      ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
                                      ANDROID_NOISE_REDUCTION_MODE,
                                      ANDROID_STATISTICS_FACE_DETECT_MODE,
                                      ANDROID_CONTROL_AF_TRIGGER,
                                      ANDROID_REQUEST_ID,
                                      ANDROID_SCALER_CROP_REGION,
                                      ANDROID_SENSOR_FRAME_DURATION,
#if ANDROID_SDK_VERSION < 28
                                      ANDROID_HOT_PIXEL_MODE,
#endif
                                      ANDROID_STATISTICS_HISTOGRAM_MODE,
                                      ANDROID_STATISTICS_SHARPNESS_MAP_MODE,
                                      ANDROID_TONEMAP_CURVE_BLUE,
                                      ANDROID_TONEMAP_CURVE_GREEN,
                                      ANDROID_TONEMAP_CURVE_RED,
                                      ANDROID_CONTROL_AWB_LOCK,
                                      ANDROID_CONTROL_AWB_AVAILABLE_MODES,
                                      ANDROID_COLOR_CORRECTION_TRANSFORM,
                                      ANDROID_COLOR_CORRECTION_GAINS,
                                      ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE,
                                      ANDROID_CONTROL_EFFECT_MODE,
                                      ANDROID_FLASH_STATE,
                                      ANDROID_CONTROL_AE_AVAILABLE_MODES};
    m.addInt32(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, ARRAY_SIZE(availableRequestKeys), availableRequestKeys);

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
    uint8_t shadingMode = 0;
    uint8_t tonemapMode = 0;
    uint8_t edgeMode = 0;
    uint8_t colorMode = ANDROID_COLOR_CORRECTION_MODE_FAST;
    uint8_t noiseMode = ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY;
    uint8_t aberrationMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_HIGH_QUALITY;
    uint8_t vstabMode = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF;

    switch (request_template) {
      case CAMERA3_TEMPLATE_PREVIEW:
#if ANDROID_SDK_VERSION >= 28
        hotPixelMode = ANDROID_HOT_PIXEL_MODE_FAST;
#endif
        noiseMode = ANDROID_NOISE_REDUCTION_MODE_FAST;
        aberrationMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST;
        break;
      case CAMERA3_TEMPLATE_STILL_CAPTURE:
#if ANDROID_SDK_VERSION >= 28
        hotPixelMode = ANDROID_HOT_PIXEL_MODE_HIGH_QUALITY;
#endif
        break;
      case CAMERA3_TEMPLATE_VIDEO_RECORD:
#if ANDROID_SDK_VERSION >= 28
        hotPixelMode = ANDROID_HOT_PIXEL_MODE_FAST;
#endif
        noiseMode = ANDROID_NOISE_REDUCTION_MODE_FAST;
        vstabMode = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_ON;
        aberrationMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST;
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
      case CAMERA3_TEMPLATE_MANUAL:
        noiseMode = ANDROID_NOISE_REDUCTION_MODE_FAST;
        tonemapMode = ANDROID_TONEMAP_MODE_FAST;
        colorMode = ANDROID_COLOR_CORRECTION_MODE_FAST;
        edgeMode = ANDROID_EDGE_MODE_FAST;
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
    base.addUInt8(ANDROID_COLOR_CORRECTION_ABERRATION_MODE, 1, &aberrationMode);

    /** android.noise */
    static const uint8_t noiseStrength = 5;
    base.addUInt8(ANDROID_NOISE_REDUCTION_STRENGTH, 1, &noiseStrength);

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
    int32_t cropRegion[4] = {
	     0, 0, 0, 0
    };
    base.addInt32(ANDROID_SCALER_CROP_REGION, 4, cropRegion);

    static const int32_t testPatternMode = ANDROID_SENSOR_TEST_PATTERN_MODE_OFF;
    base.addInt32(ANDROID_SENSOR_TEST_PATTERN_MODE, 1, &testPatternMode);

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
      case CAMERA3_TEMPLATE_MANUAL:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_MANUAL;
        break;
      default:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_CUSTOM;
        break;
    }
    base.addUInt8(ANDROID_CONTROL_CAPTURE_INTENT, 1, &controlIntent);

    uint8_t controlMode = ANDROID_CONTROL_MODE_AUTO;
    uint8_t aeMode = ANDROID_CONTROL_AE_MODE_ON;
    uint8_t awbMode = ANDROID_CONTROL_AWB_MODE_AUTO;
    uint8_t faceDetectMode = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;

    static const uint8_t effectMode = ANDROID_CONTROL_EFFECT_MODE_OFF;
    base.addUInt8(ANDROID_CONTROL_EFFECT_MODE, 1, &effectMode);

    static const uint8_t sceneMode = ANDROID_CONTROL_SCENE_MODE_DISABLED;
    base.addUInt8(ANDROID_CONTROL_SCENE_MODE, 1, &sceneMode);

    static const int32_t aeExpCompensation = 0;
    base.addInt32(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, 1, &aeExpCompensation);

    static const int32_t aeTargetFpsRange[2] = {
        15, 30
    };
    base.addInt32(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, 2, aeTargetFpsRange);

    static const uint8_t aeAntibandingMode =
            ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
    base.addUInt8(ANDROID_CONTROL_AE_ANTIBANDING_MODE, 1, &aeAntibandingMode);

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
      case CAMERA3_TEMPLATE_MANUAL:
        afMode = ANDROID_CONTROL_AF_MODE_OFF;
        aeMode = ANDROID_CONTROL_AE_MODE_OFF;
        awbMode = ANDROID_CONTROL_AWB_MODE_OFF;
        controlMode = ANDROID_CONTROL_MODE_OFF;
        faceDetectMode = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
        break;
      default:
        afMode = ANDROID_CONTROL_AF_MODE_AUTO;
        break;
    }

    base.addUInt8(ANDROID_CONTROL_AF_MODE, 1, &afMode);
    base.addUInt8(ANDROID_CONTROL_MODE, 1, &controlMode);
    base.addUInt8(ANDROID_CONTROL_AE_MODE, 1, &aeMode);
    base.addUInt8(ANDROID_CONTROL_AWB_MODE, 1, &awbMode);
    base.addUInt8(ANDROID_STATISTICS_FACE_DETECT_MODE, 1, &faceDetectMode);

    static const uint8_t aePrecaptureTrigger = ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;
    base.addUInt8(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, 1, &aePrecaptureTrigger);

    static const uint8_t aeLock = ANDROID_CONTROL_AE_LOCK_OFF;
    base.addUInt8(ANDROID_CONTROL_AE_LOCK, 1, &aeLock);

    static const uint8_t awbLock = ANDROID_CONTROL_AWB_LOCK_OFF;
    base.addUInt8(ANDROID_CONTROL_AWB_LOCK, 1, &awbLock);

    static const uint8_t afTrigger = ANDROID_CONTROL_AF_TRIGGER_IDLE;
    base.addUInt8(ANDROID_CONTROL_AF_TRIGGER, 1, &afTrigger);

    static const uint8_t flashState = ANDROID_FLASH_STATE_UNAVAILABLE;
    base.addUInt8(ANDROID_FLASH_STATE, 1, &flashState);

    static const uint8_t pipelineDepth = 3;
    base.addUInt8(ANDROID_REQUEST_PIPELINE_DEPTH, 1, &pipelineDepth);

    static const float colorGains[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    base.addFloat(ANDROID_COLOR_CORRECTION_GAINS, ARRAY_SIZE(colorGains), colorGains);

    static const camera_metadata_rational_t colorTransform[9] = {
        {1, 1}, {0, 1}, {0, 1}, {0, 1}, {1, 1}, {0, 1}, {0, 1}, {0, 1}, {1, 1}};
    base.addRational(ANDROID_COLOR_CORRECTION_TRANSFORM, ARRAY_SIZE(colorTransform), colorTransform);
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

