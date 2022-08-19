/*
 *  Copyright 2020 NXP.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
#define LOG_TAG "CameraMetadata"

#include <utils/Timers.h>
#include "CameraMetadata.h"
#include "CameraDeviceHWLImpl.h"
#include "ISPCameraDeviceHWLImpl.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define MAX_RESOLUTION_SIZE 64

namespace android {

HalCameraMetadata* CameraMetadata::GetStaticMeta()
{
    return m_static_meta.get();
}

CameraMetadata* CameraMetadata::Clone()
{
    CameraMetadata *pMeta = new CameraMetadata();

    pMeta->m_static_meta = HalCameraMetadata::Clone(m_static_meta.get());

    for (uint32_t i = (uint32_t)RequestTemplate::kPreview;
         i <= (uint32_t)RequestTemplate::kManual;
         i++) {
        pMeta->m_template_meta[i] = HalCameraMetadata::Clone(m_template_meta[i].get());
    }

    return pMeta;
}

status_t CameraMetadata::createMetadata(CameraDeviceHwlImpl *pDev, CameraSensorMetadata mSensorData)
{
    /*
     * Setup static camera info.  This will have to customized per camera
     * device.
     */

    if(pDev == NULL)
        return BAD_VALUE;

    m_static_meta = HalCameraMetadata::Create(1, 10);

    /* android.control */
    m_static_meta->Set(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
                     pDev->mTargetFpsRange,
                     pDev->mFpsRangeCount);

    static const uint8_t aeAntibandingMode =
        ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
    m_static_meta->Set(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES, &aeAntibandingMode, 1);

    int32_t android_control_ae_compensation_range[] = {mSensorData.mAeCompMin, mSensorData.mAeCompMax};
    m_static_meta->Set(ANDROID_CONTROL_AE_COMPENSATION_RANGE,
                     android_control_ae_compensation_range,
                     ARRAY_SIZE(android_control_ae_compensation_range));

    camera_metadata_rational_t android_control_ae_compensation_step[] = {{mSensorData.mAeCompStepNumerator, mSensorData.mAeCompStepDenominator}};
    m_static_meta->Set(ANDROID_CONTROL_AE_COMPENSATION_STEP,
                     android_control_ae_compensation_step,
                     ARRAY_SIZE(android_control_ae_compensation_step));

    int32_t android_control_max_regions[] = {/*AE*/ 0, /*AWB*/ 0, /*AF*/ 0};
    m_static_meta->Set(ANDROID_CONTROL_MAX_REGIONS,
                     android_control_max_regions,
                     ARRAY_SIZE(android_control_max_regions));

    /* android.jpeg */
    int32_t android_jpeg_available_thumbnail_sizes[] = {0, 0, 96, 96, 160, 90, 160, 120};
    m_static_meta->Set(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
                     android_jpeg_available_thumbnail_sizes,
                     ARRAY_SIZE(android_jpeg_available_thumbnail_sizes));

    int32_t android_jpeg_max_size[] = {mSensorData.maxjpegsize};
    m_static_meta->Set(ANDROID_JPEG_MAX_SIZE,
                     android_jpeg_max_size,
                     ARRAY_SIZE(android_jpeg_max_size));

    /* android.lens */
    float android_lens_info_available_focal_lengths[] = {mSensorData.focallength};
    m_static_meta->Set(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
                     android_lens_info_available_focal_lengths,
                     ARRAY_SIZE(android_lens_info_available_focal_lengths));

    float android_lens_info_available_apertures[] = {2.8};
    m_static_meta->Set(ANDROID_LENS_INFO_AVAILABLE_APERTURES,
                     android_lens_info_available_apertures,
                     ARRAY_SIZE(android_lens_info_available_apertures));

    float android_lens_info_available_filter_densities[] = {0.0};
    m_static_meta->Set(ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES,
                     android_lens_info_available_filter_densities,
                     ARRAY_SIZE(android_lens_info_available_filter_densities));

    uint8_t android_lens_info_available_optical_stabilization[] = {
        ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF};
    m_static_meta->Set(ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
                     android_lens_info_available_optical_stabilization,
                     ARRAY_SIZE(android_lens_info_available_optical_stabilization));

    uint8_t android_lens_info_focus_distance_calibration[] = {
            ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION_CALIBRATED};
    m_static_meta->Set(ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION,
            android_lens_info_focus_distance_calibration,
            ARRAY_SIZE(android_lens_info_focus_distance_calibration));

    // Ref https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#LENS_INFO_FOCUS_DISTANCE_CALIBRATION
    // APPROXIMATE and CALIBRATED devices report the focus metadata in units of diopters (1/meter), so 0.0f represents focusing at infinity.
    float android_lens_info_hyperfocal_distance[] = {0.0};
    m_static_meta->Set(ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE,
            android_lens_info_hyperfocal_distance,
            ARRAY_SIZE(android_lens_info_hyperfocal_distance));

    // Ref https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#LENS_INFO_MINIMUM_FOCUS_DISTANCE
    // If the lens is fixed-focus, this will be 0.
    float android_lens_info_minimum_focus_distance[] = {0.0};
    m_static_meta->Set(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
            android_lens_info_minimum_focus_distance,
            ARRAY_SIZE(android_lens_info_minimum_focus_distance));

    /* android.request */
    int32_t android_request_max_num_output_streams[] = {0, 3, 1};
    m_static_meta->Set(ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS,
                     android_request_max_num_output_streams,
                     ARRAY_SIZE(android_request_max_num_output_streams));

    /* android.scaler */
    m_static_meta->Set(ANDROID_SCALER_AVAILABLE_FORMATS,
                     pDev->mAvailableFormats,
                     pDev->mAvailableFormatCount);

    int64_t android_scaler_available_jpeg_min_durations[] = {mSensorData.minframeduration};
    m_static_meta->Set(ANDROID_SCALER_AVAILABLE_JPEG_MIN_DURATIONS,
                     android_scaler_available_jpeg_min_durations,
                     ARRAY_SIZE(android_scaler_available_jpeg_min_durations));

    m_static_meta->Set(ANDROID_SCALER_AVAILABLE_JPEG_SIZES,
                     pDev->mPictureResolutions,
                     pDev->mPictureResolutionCount);

    float android_scaler_available_max_digital_zoom[] = {4};
    m_static_meta->Set(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
                     android_scaler_available_max_digital_zoom,
                     ARRAY_SIZE(android_scaler_available_max_digital_zoom));

    int64_t android_scaler_available_processed_min_durations[] = {mSensorData.minframeduration};
    m_static_meta->Set(ANDROID_SCALER_AVAILABLE_PROCESSED_MIN_DURATIONS,
                     android_scaler_available_processed_min_durations,
                     ARRAY_SIZE(android_scaler_available_processed_min_durations));

    m_static_meta->Set(ANDROID_SCALER_AVAILABLE_PROCESSED_SIZES,
                     pDev->mPreviewResolutions,
                     pDev->mPreviewResolutionCount);

    int64_t android_scaler_available_raw_min_durations[] = {mSensorData.minframeduration};
    m_static_meta->Set(ANDROID_SCALER_AVAILABLE_RAW_MIN_DURATIONS,
                     android_scaler_available_raw_min_durations,
                     ARRAY_SIZE(android_scaler_available_raw_min_durations));

    int32_t android_scaler_available_raw_sizes[] = {pDev->mMaxWidth, pDev->mMaxHeight};
    m_static_meta->Set(ANDROID_SCALER_AVAILABLE_RAW_SIZES,
                     android_scaler_available_raw_sizes,
                     ARRAY_SIZE(android_scaler_available_raw_sizes));

    /* android.sensor*/

    /* left, top, right, bottom */
    int32_t android_sensor_info_active_array_size[] = {0, 0, mSensorData.activearraywidth,  mSensorData.activearrayheight};
    m_static_meta->Set(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
                     android_sensor_info_active_array_size,
                     ARRAY_SIZE(android_sensor_info_active_array_size));

    /* left, top, right, bottom */
    int32_t android_sensor_info_precorrection_active_array_size[] = {0, 0, mSensorData.activearraywidth,  mSensorData.activearrayheight};
    m_static_meta->Set(ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE,
                     android_sensor_info_precorrection_active_array_size,
                     ARRAY_SIZE(android_sensor_info_precorrection_active_array_size));

    int32_t android_sensor_info_sensitivity_range[] =
        {100, 1600};
    m_static_meta->Set(ANDROID_SENSOR_INFO_SENSITIVITY_RANGE,
                     android_sensor_info_sensitivity_range,
                     ARRAY_SIZE(android_sensor_info_sensitivity_range));

    int64_t android_sensor_info__max_frame_duration[] = {mSensorData.maxframeduration};
    m_static_meta->Set(ANDROID_SENSOR_INFO_MAX_FRAME_DURATION,
                     android_sensor_info__max_frame_duration,
                     ARRAY_SIZE(android_sensor_info__max_frame_duration));

    int64_t kExposureTimeRange[2] = {pDev->mSensorData.mExposureNsMin, pDev->mSensorData.mExposureNsMax};
    m_static_meta->Set(ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE, kExposureTimeRange, ARRAY_SIZE(kExposureTimeRange));

    float android_sensor_info_physical_size[] = {mSensorData.physicalwidth, mSensorData.physicalheight};
    m_static_meta->Set(ANDROID_SENSOR_INFO_PHYSICAL_SIZE,
                     android_sensor_info_physical_size,
                     ARRAY_SIZE(android_sensor_info_physical_size));

    int32_t android_sensor_info_pixel_array_size[] = {mSensorData.pixelarraywidth, mSensorData.pixelarrayheight};
    m_static_meta->Set(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
                     android_sensor_info_pixel_array_size,
                     ARRAY_SIZE(android_sensor_info_pixel_array_size));

    int32_t android_sensor_orientation[] = {mSensorData.orientation};
    m_static_meta->Set(ANDROID_SENSOR_ORIENTATION,
                     android_sensor_orientation,
                     ARRAY_SIZE(android_sensor_orientation));

    /* End of static camera characteristics */

    uint8_t availableSceneModes[] = {ANDROID_CONTROL_SCENE_MODE_DISABLED};
    m_static_meta->Set(ANDROID_CONTROL_AVAILABLE_SCENE_MODES,
                     availableSceneModes,
                     ARRAY_SIZE(availableSceneModes));

    if(strstr(mSensorData.camera_name, ISP_SENSOR_NAME)) {
        uint8_t supportedHwLvl = ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL;
        m_static_meta->Set(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,
                     &supportedHwLvl,
                     1);

        uint8_t available_capabilities[] = {ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE, ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW, ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BURST_CAPTURE};
        m_static_meta->Set(ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                     available_capabilities,
                     ARRAY_SIZE(available_capabilities));

        // Ref https://developer.android.com/reference/android/hardware/camera2/CameraMetadata#SYNC_MAX_LATENCY_PER_FRAME_CONTROL
        // "full" level device must support ANDROID_SYNC_MAX_LATENCY_PER_FRAME_CONTROL.
        static const int32_t maxLatency = ANDROID_SYNC_MAX_LATENCY_PER_FRAME_CONTROL;
        m_static_meta->Set(ANDROID_SYNC_MAX_LATENCY, &maxLatency, 1);

        const uint8_t color_arrange = ((ISPCameraDeviceHwlImpl*)pDev)->m_color_arrange;
        m_static_meta->Set(ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT, &color_arrange, 1);

        // Ref "blsData" in DAA3840_30MC_1080P.xml
        int32_t android_sensor_black_level_pattern[] = {168, 168, 168, 168};
        m_static_meta->Set(ANDROID_SENSOR_BLACK_LEVEL_PATTERN,
                        android_sensor_black_level_pattern,
                        ARRAY_SIZE(android_sensor_black_level_pattern));

        int32_t white_level = 4095; // basler use 12bit raw data
        m_static_meta->Set(ANDROID_SENSOR_INFO_WHITE_LEVEL, &white_level, 1);

        uint8_t ref_illum = ANDROID_SENSOR_REFERENCE_ILLUMINANT1_DAYLIGHT;
        m_static_meta->Set(ANDROID_SENSOR_REFERENCE_ILLUMINANT1, &ref_illum, 1);

        // color matrix, transforms color values from CIE XYZ color space to reference sensor color space.
        // Ref https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#SENSOR_COLOR_TRANSFORM1
        // The value refs from emu_camera_back.json.
        camera_metadata_rational_t android_sensor_color_tramsform1[] = { {3209, 1024}, {-1655, 1024}, {-502, 1024}, \
                                                                         {-1002, 1024}, {1962, 1024}, {34, 1024}, \
                                                                         {73, 1024}, {-234, 1024}, {1438, 1024} };
        m_static_meta->Set(ANDROID_SENSOR_COLOR_TRANSFORM1,
                        android_sensor_color_tramsform1,
                        ARRAY_SIZE(android_sensor_color_tramsform1));

        // forward matrix, transforms white balanced camera colors from the reference sensor colorspace to the CIE XYZ colorspace with a D50 whitepoint. 
        // Ref https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#SENSOR_FORWARD_MATRIX1
        // The value refs from emu_camera_back.json.
        camera_metadata_rational_t android_sensor_forward_matrix1[] = { {446, 1024}, {394, 1024}, {146, 1024}, \
                                                                           {227, 1024}, {734, 1024}, {62, 1024}, \
                                                                           {14, 1024}, {99, 1024}, {731, 1024} };
        m_static_meta->Set(ANDROID_SENSOR_FORWARD_MATRIX1,
                        android_sensor_forward_matrix1,
                        ARRAY_SIZE(android_sensor_forward_matrix1));


        // calib matrix, transform matrix that maps from the reference sensor colorspace to the actual device sensor colorspace.
        // Ref https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#SENSOR_CALIBRATION_TRANSFORM1
        camera_metadata_rational_t android_sensor_calib_tramsform1[] = { {1, 1}, {0, 1}, {0, 1}, \
                                                                         {0, 1}, {1, 1}, {0, 1}, \
                                                                         {0, 1}, {0, 1}, {1, 1} };
        m_static_meta->Set(ANDROID_SENSOR_CALIBRATION_TRANSFORM1,
                        android_sensor_calib_tramsform1,
                        ARRAY_SIZE(android_sensor_calib_tramsform1));

        int32_t correction_active_array_size[] = {0, 0, pDev->mMaxWidth, pDev->mMaxHeight};

        m_static_meta->Set(ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE,
                         correction_active_array_size,
                         ARRAY_SIZE(correction_active_array_size));

        // Ref https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES
        uint8_t availableHotPixelMapModes[] = {ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF};
        m_static_meta->Set(ANDROID_STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES, availableHotPixelMapModes, ARRAY_SIZE(availableHotPixelMapModes));

        // Ref https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#CONTROL_POST_RAW_SENSITIVITY_BOOST_RANGE
        int32_t post_raw_sensitivity_boot_range[] = {100, 100};
        m_static_meta->Set(ANDROID_CONTROL_POST_RAW_SENSITIVITY_BOOST_RANGE, post_raw_sensitivity_boot_range, ARRAY_SIZE(post_raw_sensitivity_boot_range));

        // Ref https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#SENSOR_MAX_ANALOG_SENSITIVITY
        // The value is refed from emu_camera_back.json.
        int32_t max_analog_sensitivity = 1600;
        m_static_meta->Set(ANDROID_SENSOR_MAX_ANALOG_SENSITIVITY, &max_analog_sensitivity, 1);

        // Ref https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#SHADING_AVAILABLE_MODES
        uint8_t available_shading_modes[] = {ANDROID_SHADING_MODE_OFF, ANDROID_SHADING_MODE_FAST};
        m_static_meta->Set(ANDROID_SHADING_AVAILABLE_MODES,
                     available_shading_modes,
                     ARRAY_SIZE(available_shading_modes));

        // Ref https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES
        uint8_t available_lens_shading_map_modes[] = {ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF, ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_ON};
        m_static_meta->Set(ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,
                     available_lens_shading_map_modes,
                     ARRAY_SIZE(available_lens_shading_map_modes));

        float android_control_zoom_ratio_range[] = {1.0, 4.0};
        m_static_meta->Set(ANDROID_CONTROL_ZOOM_RATIO_RANGE,
                     android_control_zoom_ratio_range,
                     ARRAY_SIZE(android_control_zoom_ratio_range));


        uint8_t availableSceneModes[] = {ANDROID_CONTROL_SCENE_MODE_DISABLED, ANDROID_CONTROL_SCENE_MODE_HDR};
        m_static_meta->Set(ANDROID_CONTROL_AVAILABLE_SCENE_MODES,
                     availableSceneModes,
                     ARRAY_SIZE(availableSceneModes));

        uint8_t sensor_info_lens_shading_applied = ANDROID_SENSOR_INFO_LENS_SHADING_APPLIED_FALSE;
        m_static_meta->Set(ANDROID_SENSOR_INFO_LENS_SHADING_APPLIED, &sensor_info_lens_shading_applied, 1);

        static const uint8_t availableToneMapModes[] = {ANDROID_TONEMAP_MODE_CONTRAST_CURVE, ANDROID_TONEMAP_MODE_FAST, ANDROID_TONEMAP_MODE_HIGH_QUALITY, ANDROID_TONEMAP_MODE_GAMMA_VALUE};
        m_static_meta->Set(ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES, availableToneMapModes, ARRAY_SIZE(availableToneMapModes));
    } else {
        uint8_t supportedHwLvl = ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LEGACY;
        m_static_meta->Set(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,
                     &supportedHwLvl,
                     1);

        uint8_t available_capabilities[] = {ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE};
        m_static_meta->Set(ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                     available_capabilities,
                     ARRAY_SIZE(available_capabilities));

        static const int32_t maxLatency = ANDROID_SYNC_MAX_LATENCY_UNKNOWN;
        m_static_meta->Set(ANDROID_SYNC_MAX_LATENCY, &maxLatency, 1);

        static const uint8_t availableToneMapModes[] = {ANDROID_TONEMAP_MODE_CONTRAST_CURVE, ANDROID_TONEMAP_MODE_FAST, ANDROID_TONEMAP_MODE_HIGH_QUALITY};
        m_static_meta->Set(ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES, availableToneMapModes, ARRAY_SIZE(availableToneMapModes));
    }


    uint8_t lensFacing = ANDROID_LENS_FACING_BACK;
    if(strstr(mSensorData.camera_type, "front"))
        lensFacing = ANDROID_LENS_FACING_FRONT;

    m_static_meta->Set(ANDROID_LENS_FACING,
                     &lensFacing,
                     1);

    int ResIdx = 0;
    int ResCount = 0;
    int streamConfigIdx = 0;
    int32_t streamConfig[MAX_RESOLUTION_SIZE * 6];  // MAX_RESOLUTION_SIZE/2 * 2 * 6;
    int64_t minFrmDuration[MAX_RESOLUTION_SIZE * 6];
    int64_t stallDuration[MAX_RESOLUTION_SIZE * 6];

    // TODO: It's better to get those info in seperate camera, and get the accurate fps.
    ResCount = pDev->mPreviewResolutionCount / 2;
    for (ResIdx = 0; ResIdx < ResCount; ResIdx++) {
        streamConfigIdx = ResIdx * 4;

        streamConfig[streamConfigIdx] = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
        streamConfig[streamConfigIdx + 1] = pDev->mPreviewResolutions[ResIdx * 2];
        streamConfig[streamConfigIdx + 2] = pDev->mPreviewResolutions[ResIdx * 2 + 1];
        streamConfig[streamConfigIdx + 3] = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;

        minFrmDuration[streamConfigIdx] = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
        minFrmDuration[streamConfigIdx + 1] = pDev->mPreviewResolutions[ResIdx * 2];
        minFrmDuration[streamConfigIdx + 2] = pDev->mPreviewResolutions[ResIdx * 2 + 1];
        minFrmDuration[streamConfigIdx + 3] = mSensorData.minframeduration;  // ns

        stallDuration[streamConfigIdx] = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
        stallDuration[streamConfigIdx + 1] = pDev->mPreviewResolutions[ResIdx * 2];
        stallDuration[streamConfigIdx + 2] = pDev->mPreviewResolutions[ResIdx * 2 + 1];
        stallDuration[streamConfigIdx + 3] = 0;  // ns
    }

    ResCount = pDev->mPictureResolutionCount / 2;
    for (ResIdx = 0; ResIdx < ResCount; ResIdx++) {
        streamConfigIdx = pDev->mPreviewResolutionCount * 2 + ResIdx * 4;

        streamConfig[streamConfigIdx] = HAL_PIXEL_FORMAT_BLOB;
        streamConfig[streamConfigIdx + 1] = pDev->mPictureResolutions[ResIdx * 2];
        streamConfig[streamConfigIdx + 2] = pDev->mPictureResolutions[ResIdx * 2 + 1];
        streamConfig[streamConfigIdx + 3] = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;

        minFrmDuration[streamConfigIdx] = HAL_PIXEL_FORMAT_BLOB;
        minFrmDuration[streamConfigIdx + 1] = pDev->mPictureResolutions[ResIdx * 2];
        minFrmDuration[streamConfigIdx + 2] = pDev->mPictureResolutions[ResIdx * 2 + 1];
        minFrmDuration[streamConfigIdx + 3] = mSensorData.minframeduration;  // ns
        if ((minFrmDuration[streamConfigIdx + 1] == 2592) && (minFrmDuration[streamConfigIdx + 2]  == 1944) &&
           (mSensorData.minframeduration_blob_5M > 0)) {
            minFrmDuration[streamConfigIdx + 3] = mSensorData.minframeduration_blob_5M;
            ALOGI("%s: set minFrmDuration of 5M picture to %ld ns", __func__, mSensorData.minframeduration_blob_5M);
        }

        stallDuration[streamConfigIdx] = HAL_PIXEL_FORMAT_BLOB;
        stallDuration[streamConfigIdx + 1] = pDev->mPictureResolutions[ResIdx * 2];
        stallDuration[streamConfigIdx + 2] = pDev->mPictureResolutions[ResIdx * 2 + 1];
        stallDuration[streamConfigIdx + 3] = mSensorData.minframeduration;  // ns
    }

    ResCount = pDev->mPreviewResolutionCount / 2;
    for (ResIdx = 0; ResIdx < ResCount; ResIdx++) {
        streamConfigIdx = pDev->mPictureResolutionCount * 2 + pDev->mPreviewResolutionCount * 2 + ResIdx * 4;

        streamConfig[streamConfigIdx] = HAL_PIXEL_FORMAT_YCBCR_420_888;
        streamConfig[streamConfigIdx + 1] = pDev->mPreviewResolutions[ResIdx * 2];
        streamConfig[streamConfigIdx + 2] = pDev->mPreviewResolutions[ResIdx * 2 + 1];
        streamConfig[streamConfigIdx + 3] = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;

        minFrmDuration[streamConfigIdx] = HAL_PIXEL_FORMAT_YCBCR_420_888;
        minFrmDuration[streamConfigIdx + 1] = pDev->mPreviewResolutions[ResIdx * 2];
        minFrmDuration[streamConfigIdx + 2] = pDev->mPreviewResolutions[ResIdx * 2 + 1];
        minFrmDuration[streamConfigIdx + 3] = mSensorData.minframeduration;  // ns

        stallDuration[streamConfigIdx] = HAL_PIXEL_FORMAT_YCBCR_420_888;
        stallDuration[streamConfigIdx + 1] = pDev->mPreviewResolutions[ResIdx * 2];
        stallDuration[streamConfigIdx + 2] = pDev->mPreviewResolutions[ResIdx * 2 + 1];
        stallDuration[streamConfigIdx + 3] = 0;
    }

    // add raw format for isp camera
    if(strstr(mSensorData.camera_name, ISP_SENSOR_NAME)) {
        streamConfigIdx += 4;
        streamConfig[streamConfigIdx] = HAL_PIXEL_FORMAT_RAW16;
        streamConfig[streamConfigIdx + 1] = pDev->mMaxWidth;
        streamConfig[streamConfigIdx + 2] = pDev->mMaxHeight;
        streamConfig[streamConfigIdx + 3] = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;

        minFrmDuration[streamConfigIdx] = HAL_PIXEL_FORMAT_RAW16;
        minFrmDuration[streamConfigIdx + 1] = pDev->mMaxWidth;
        minFrmDuration[streamConfigIdx + 2] = pDev->mMaxHeight;
        minFrmDuration[streamConfigIdx + 3] = mSensorData.minframeduration;  // ns

        stallDuration[streamConfigIdx] = HAL_PIXEL_FORMAT_RAW16;
        stallDuration[streamConfigIdx + 1] = pDev->mMaxWidth;
        stallDuration[streamConfigIdx + 2] = pDev->mMaxHeight;
        stallDuration[streamConfigIdx + 3] = mSensorData.minframeduration;   // ns
    }

    m_static_meta->Set(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                     streamConfig,
                     streamConfigIdx + 4);

    m_static_meta->Set(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
                     minFrmDuration,
                     streamConfigIdx + 4);

    m_static_meta->Set(ANDROID_SCALER_AVAILABLE_STALL_DURATIONS,
                     stallDuration,
                     streamConfigIdx + 4);

    static const uint8_t croppingType = ANDROID_SCALER_CROPPING_TYPE_FREEFORM;
    m_static_meta->Set(ANDROID_SCALER_CROPPING_TYPE, &croppingType, 1);

    static const int32_t tonemapCurvePoints = 128;
    m_static_meta->Set(ANDROID_TONEMAP_MAX_CURVE_POINTS, &tonemapCurvePoints, 1);

    static const uint8_t afTrigger = ANDROID_CONTROL_AF_TRIGGER_IDLE;
    m_static_meta->Set(ANDROID_CONTROL_AF_TRIGGER, &afTrigger, 1);

    static const uint8_t tonemapMode = ANDROID_TONEMAP_MODE_FAST;
    m_static_meta->Set(ANDROID_TONEMAP_MODE, &tonemapMode, 1);

    static const uint8_t pipelineMaxDepth = 3;
    m_static_meta->Set(ANDROID_REQUEST_PIPELINE_MAX_DEPTH, &pipelineMaxDepth, 1);

    static const int32_t partialResultCount = 1;
    m_static_meta->Set(ANDROID_REQUEST_PARTIAL_RESULT_COUNT, &partialResultCount, 1);

    static const uint8_t timestampSource = ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME;
    m_static_meta->Set(ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE, &timestampSource, 1);

    static const int32_t maxFaceCount = 0;
    m_static_meta->Set(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT, &maxFaceCount, 1);

#if ANDROID_SDK_VERSION >= 28

    static const uint8_t aeLockAvailable = ANDROID_CONTROL_AE_LOCK_AVAILABLE_TRUE;
    m_static_meta->Set(ANDROID_CONTROL_AE_LOCK_AVAILABLE, &aeLockAvailable, 1);

    static const uint8_t awbLockAvailable = ANDROID_CONTROL_AWB_LOCK_AVAILABLE_TRUE;
    m_static_meta->Set(ANDROID_CONTROL_AWB_LOCK_AVAILABLE, &awbLockAvailable, 1);

    static const uint8_t availableControlModes[] = {ANDROID_CONTROL_MODE_OFF, ANDROID_CONTROL_MODE_AUTO, ANDROID_CONTROL_MODE_USE_SCENE_MODE};
    m_static_meta->Set(ANDROID_CONTROL_AVAILABLE_MODES, availableControlModes, ARRAY_SIZE(availableControlModes));

    static const uint8_t availableHotPixelModes[] = {ANDROID_HOT_PIXEL_MODE_FAST, ANDROID_HOT_PIXEL_MODE_HIGH_QUALITY};
    m_static_meta->Set(ANDROID_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES, availableHotPixelModes, ARRAY_SIZE(availableHotPixelModes));

    static const int32_t session_keys[] = {ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, ANDROID_CONTROL_AE_TARGET_FPS_RANGE};
    m_static_meta->Set(ANDROID_REQUEST_AVAILABLE_SESSION_KEYS, session_keys, ARRAY_SIZE(session_keys));
#endif

    int32_t availableResultKeys[] = {ANDROID_SENSOR_TIMESTAMP, ANDROID_FLASH_STATE, ANDROID_CONTROL_ZOOM_RATIO};
    if (strstr(mSensorData.camera_name, ISP_SENSOR_NAME) == NULL) {
        m_static_meta->Set(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS, availableResultKeys, ARRAY_SIZE(availableResultKeys));
    } else {
        int32_t availableResultISPKeys[] = {ANDROID_SENSOR_NOISE_PROFILE, ANDROID_SENSOR_GREEN_SPLIT, ANDROID_SENSOR_NEUTRAL_COLOR_POINT};
        MergeAndSetMeta(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
                        availableResultKeys, sizeof(availableResultKeys),
                        availableResultISPKeys, sizeof(availableResultISPKeys));
    }

    static const uint8_t availableVstabModes[] = {ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF};
    m_static_meta->Set(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES, availableVstabModes, ARRAY_SIZE(availableVstabModes));

    static const uint8_t availableEffects[] = {ANDROID_CONTROL_EFFECT_MODE_OFF};
    m_static_meta->Set(ANDROID_CONTROL_AVAILABLE_EFFECTS, availableEffects, ARRAY_SIZE(availableEffects));

    static const int32_t availableTestPatternModes[] = {ANDROID_SENSOR_TEST_PATTERN_MODE_OFF};
    m_static_meta->Set(ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES, availableTestPatternModes, ARRAY_SIZE(availableTestPatternModes));

    static const uint8_t availableEdgeModes[] = {ANDROID_EDGE_MODE_OFF, ANDROID_EDGE_MODE_FAST, ANDROID_EDGE_MODE_HIGH_QUALITY};
    m_static_meta->Set(ANDROID_EDGE_AVAILABLE_EDGE_MODES, availableEdgeModes, ARRAY_SIZE(availableEdgeModes));

    static const uint8_t availableNoiseReductionModes[] = {ANDROID_NOISE_REDUCTION_MODE_OFF, ANDROID_NOISE_REDUCTION_MODE_FAST, ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY};
    m_static_meta->Set(ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES, availableNoiseReductionModes, ARRAY_SIZE(availableNoiseReductionModes));

    static const uint8_t availableAeModes[] = {ANDROID_CONTROL_AE_MODE_OFF, ANDROID_CONTROL_AE_MODE_ON};
    m_static_meta->Set(ANDROID_CONTROL_AE_AVAILABLE_MODES, availableAeModes, ARRAY_SIZE(availableAeModes));

    static const uint8_t availableAfModes[] = {ANDROID_CONTROL_AF_MODE_OFF};
    m_static_meta->Set(ANDROID_CONTROL_AF_AVAILABLE_MODES, availableAfModes, ARRAY_SIZE(availableAfModes));

    static const uint8_t availableAwbModes[] = {
        ANDROID_CONTROL_AWB_MODE_OFF,
        ANDROID_CONTROL_AWB_MODE_AUTO,
        ANDROID_CONTROL_AWB_MODE_INCANDESCENT,
        ANDROID_CONTROL_AWB_MODE_FLUORESCENT,
        ANDROID_CONTROL_AWB_MODE_DAYLIGHT,
        ANDROID_CONTROL_AWB_MODE_SHADE};
    m_static_meta->Set(ANDROID_CONTROL_AWB_AVAILABLE_MODES, availableAwbModes, ARRAY_SIZE(availableAwbModes));

    static const uint8_t availableAberrationModes[] = {
        ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF,
        ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST,
        ANDROID_COLOR_CORRECTION_ABERRATION_MODE_HIGH_QUALITY};
    m_static_meta->Set(ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
                     availableAberrationModes,
                     ARRAY_SIZE(availableAberrationModes));

    /* flahs info */
    uint8_t flashInfoAvailable = ANDROID_FLASH_INFO_AVAILABLE_FALSE;
    m_static_meta->Set(ANDROID_FLASH_INFO_AVAILABLE, &flashInfoAvailable, 1);

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
        ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
        ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION,
        ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE,
        ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
        ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
        ANDROID_REQUEST_PARTIAL_RESULT_COUNT,
        ANDROID_REQUEST_PIPELINE_MAX_DEPTH,
        ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE,
        ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES,
        ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
        ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE,
        ANDROID_SENSOR_INFO_PHYSICAL_SIZE,
        ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
        ANDROID_SENSOR_ORIENTATION,
        ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
        ANDROID_STATISTICS_INFO_MAX_FACE_COUNT,
        ANDROID_SYNC_MAX_LATENCY
    };

    if (strstr(mSensorData.camera_name, ISP_SENSOR_NAME) == NULL) {
        m_static_meta->Set(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
                     characteristics_keys_basic,
                     ARRAY_SIZE(characteristics_keys_basic));
    }
    else {
        int32_t characteristics_keys_isp[] = {
            ANDROID_CONTROL_POST_RAW_SENSITIVITY_BOOST,
            ANDROID_CONTROL_POST_RAW_SENSITIVITY_BOOST_RANGE,
            ANDROID_CONTROL_ZOOM_RATIO_RANGE,
            ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT,
            ANDROID_SENSOR_BLACK_LEVEL_PATTERN,
            ANDROID_SENSOR_INFO_WHITE_LEVEL,
            ANDROID_SENSOR_REFERENCE_ILLUMINANT1,
            ANDROID_SENSOR_COLOR_TRANSFORM1,
            ANDROID_SENSOR_FORWARD_MATRIX1,
            ANDROID_SENSOR_CALIBRATION_TRANSFORM1,
            ANDROID_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES,
            ANDROID_STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES,
            ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE,
            ANDROID_EDGE_AVAILABLE_EDGE_MODES,
            ANDROID_LENS_INFO_AVAILABLE_APERTURES,
            ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES,
            ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE,
            ANDROID_SENSOR_INFO_MAX_FRAME_DURATION,
            ANDROID_SENSOR_INFO_SENSITIVITY_RANGE,
            ANDROID_SENSOR_MAX_ANALOG_SENSITIVITY,
            ANDROID_SHADING_AVAILABLE_MODES,
            ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,
            ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES,
            ANDROID_TONEMAP_MAX_CURVE_POINTS,
        };

        MergeAndSetMeta(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
            characteristics_keys_basic, sizeof(characteristics_keys_basic),
            characteristics_keys_isp, sizeof(characteristics_keys_isp));
    }

    /* face detect mode */
    uint8_t availableFaceDetectModes[] = {ANDROID_STATISTICS_FACE_DETECT_MODE_OFF};
    m_static_meta->Set(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
                     availableFaceDetectModes,
                     ARRAY_SIZE(availableFaceDetectModes));

    int32_t availableRequestKeys[] = { ANDROID_COLOR_CORRECTION_MODE,
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
                                       ANDROID_LENS_OPTICAL_STABILIZATION_MODE,
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
                                       ANDROID_CONTROL_ZOOM_RATIO,
                                       ANDROID_CONTROL_AE_AVAILABLE_MODES };

    if (strstr(mSensorData.camera_name, ISP_SENSOR_NAME) == NULL)
        m_static_meta->Set(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, availableRequestKeys, ARRAY_SIZE(availableRequestKeys));
    else {
        int32_t availableRequestISPKeys[] = { ANDROID_HOT_PIXEL_MODE,
                                              ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE,
                                              ANDROID_EDGE_MODE,
                                              ANDROID_TONEMAP_MODE,
                                              ANDROID_LENS_FILTER_DENSITY,
                                              ANDROID_LENS_APERTURE };

        MergeAndSetMeta(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS,
                      availableRequestKeys, sizeof(availableRequestKeys),
                      availableRequestISPKeys, sizeof(availableRequestISPKeys));
    }

    return OK;
}

status_t CameraMetadata::MergeAndSetMeta(uint32_t tag, int32_t* array_keys_basic, int basic_size, int* array_keys_isp, int isp_size)
{
    int total_size = basic_size + isp_size;

    if((array_keys_basic == NULL) || (array_keys_isp == NULL) || (basic_size == 0) || (isp_size == 0))
        return BAD_VALUE;

    int32_t *pIspTotalKeys = (int32_t *)malloc(total_size);
    if(pIspTotalKeys == NULL) {
        ALOGE("%s, malloc %d bytes for pIspTotalKeys failed", __func__, total_size);
        return BAD_VALUE;
    }

    memcpy(pIspTotalKeys, array_keys_basic, basic_size);
    memcpy((uint8_t *)pIspTotalKeys + basic_size, array_keys_isp, isp_size);

    m_static_meta->Set(tag, pIspTotalKeys, total_size/sizeof(int32_t));

    free(pIspTotalKeys);

    return OK;
}

status_t CameraMetadata::createSettingTemplate(std::unique_ptr<HalCameraMetadata>& base,
                                               RequestTemplate type, CameraSensorMetadata mSensorData)
{
    /** android.request */
    static const uint8_t metadataMode = ANDROID_REQUEST_METADATA_MODE_NONE;
    base->Set(ANDROID_REQUEST_METADATA_MODE, &metadataMode, 1);

    static const int32_t id = 0;
    base->Set(ANDROID_REQUEST_ID, &id, 1);

    static const int32_t frameCount = 0;
    base->Set(ANDROID_REQUEST_FRAME_COUNT, &frameCount, 1);

    /** android.lens */
    static const float focusDistance = 0;
    base->Set(ANDROID_LENS_FOCUS_DISTANCE, &focusDistance, 1);

    static float aperture = 2.8;
    base->Set(ANDROID_LENS_APERTURE, &aperture, 1);
    base->Set(ANDROID_LENS_FOCAL_LENGTH, &(mSensorData.focallength), 1);

    static const float filterDensity = 0;
    base->Set(ANDROID_LENS_FILTER_DENSITY, &filterDensity, 1);

    static const uint8_t opticalStabilizationMode =
        ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
    base->Set(ANDROID_LENS_OPTICAL_STABILIZATION_MODE,
              &opticalStabilizationMode,
              1);

    /** android.sensor */
    static const int64_t frameDuration = mSensorData.minframeduration;
    base->Set(ANDROID_SENSOR_FRAME_DURATION, &frameDuration, 1);

    /** android.flash */
    static const uint8_t flashMode = ANDROID_FLASH_MODE_OFF;
    base->Set(ANDROID_FLASH_MODE, &flashMode, 1);

    static const uint8_t flashPower = 10;
    base->Set(ANDROID_FLASH_FIRING_POWER, &flashPower, 1);

    static const int64_t firingTime = 0;
    base->Set(ANDROID_FLASH_FIRING_TIME, &firingTime, 1);

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

    switch (type) {
        case RequestTemplate::kPreview:
#if ANDROID_SDK_VERSION >= 28
            hotPixelMode = ANDROID_HOT_PIXEL_MODE_FAST;
#endif
            noiseMode = ANDROID_NOISE_REDUCTION_MODE_FAST;
            aberrationMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST;
            break;
        case RequestTemplate::kStillCapture:
#if ANDROID_SDK_VERSION >= 28
            hotPixelMode = ANDROID_HOT_PIXEL_MODE_HIGH_QUALITY;
#endif
            break;
        case RequestTemplate::kVideoRecord:
#if ANDROID_SDK_VERSION >= 28
            hotPixelMode = ANDROID_HOT_PIXEL_MODE_FAST;
#endif
            noiseMode = ANDROID_NOISE_REDUCTION_MODE_FAST;
            vstabMode = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_ON;
            aberrationMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST;
            break;
        case RequestTemplate::kVideoSnapshot:
            vstabMode = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_ON;
            break;
        case RequestTemplate::kZeroShutterLag:
            hotPixelMode = ANDROID_HOT_PIXEL_MODE_HIGH_QUALITY;
            demosaicMode = ANDROID_DEMOSAIC_MODE_HIGH_QUALITY;
            noiseMode = ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY;
            shadingMode = ANDROID_SHADING_MODE_HIGH_QUALITY;
            colorMode = ANDROID_COLOR_CORRECTION_MODE_HIGH_QUALITY;
            tonemapMode = ANDROID_TONEMAP_MODE_HIGH_QUALITY;
            edgeMode = ANDROID_EDGE_MODE_HIGH_QUALITY;
            break;
        case RequestTemplate::kManual:
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

    if (strstr(mSensorData.camera_name, ISP_SENSOR_NAME)) {
        switch (type) {
            case RequestTemplate::kPreview:
                tonemapMode = ANDROID_TONEMAP_MODE_FAST;
                edgeMode = ANDROID_EDGE_MODE_FAST;
                break;
            case RequestTemplate::kStillCapture:
                tonemapMode = ANDROID_TONEMAP_MODE_HIGH_QUALITY;
                edgeMode = ANDROID_EDGE_MODE_HIGH_QUALITY;
                break;
            case RequestTemplate::kVideoRecord:
                tonemapMode = ANDROID_TONEMAP_MODE_FAST;
                edgeMode = ANDROID_EDGE_MODE_FAST;
                break;
            case RequestTemplate::kVideoSnapshot:
                tonemapMode = ANDROID_TONEMAP_MODE_FAST;
                break;
            default:
                break;
        }
    }

    base->Set(ANDROID_HOT_PIXEL_MODE, &hotPixelMode, 1);
    base->Set(ANDROID_DEMOSAIC_MODE, &demosaicMode, 1);
    base->Set(ANDROID_NOISE_REDUCTION_MODE, &noiseMode, 1);
    base->Set(ANDROID_SHADING_MODE, &shadingMode, 1);
    base->Set(ANDROID_COLOR_CORRECTION_MODE, &colorMode, 1);
    base->Set(ANDROID_TONEMAP_MODE, &tonemapMode, 1);
    base->Set(ANDROID_EDGE_MODE, &edgeMode, 1);
    base->Set(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, &vstabMode, 1);
    base->Set(ANDROID_COLOR_CORRECTION_ABERRATION_MODE, &aberrationMode, 1);

    /** android.noise */
    static const uint8_t noiseStrength = 5;
    base->Set(ANDROID_NOISE_REDUCTION_STRENGTH, &noiseStrength, 1);

    /** android.tonemap */
    static const float tonemapCurve[4] = {
        0.f, 0.f, 1.f, 1.f};
    base->Set(ANDROID_TONEMAP_CURVE_RED, tonemapCurve, 4);  // sungjoong
    base->Set(ANDROID_TONEMAP_CURVE_GREEN, tonemapCurve, 4);
    base->Set(ANDROID_TONEMAP_CURVE_BLUE, tonemapCurve, 4);

    /** android.edge */
    static const uint8_t edgeStrength = 5;
    base->Set(ANDROID_EDGE_STRENGTH, &edgeStrength, 1);

    /** android.scaler */
    int32_t cropRegion[4] = {
        0, 0, 0, 0};
    base->Set(ANDROID_SCALER_CROP_REGION, cropRegion, 4);

    static const int32_t testPatternMode = ANDROID_SENSOR_TEST_PATTERN_MODE_OFF;
    base->Set(ANDROID_SENSOR_TEST_PATTERN_MODE, &testPatternMode, 1);

    /** android.jpeg */
    //4.3 framework change quality type from i32 to u8
    static const uint8_t jpegQuality = 100;
    base->Set(ANDROID_JPEG_QUALITY, &jpegQuality, 1);

    static const int32_t thumbnailSize[2] = {
        160, 120};
    base->Set(ANDROID_JPEG_THUMBNAIL_SIZE, thumbnailSize, 2);

    //4.3 framework change quality type from i32 to u8
    static const uint8_t thumbnailQuality = 100;
    base->Set(ANDROID_JPEG_THUMBNAIL_QUALITY, &thumbnailQuality, 1);

    static const double gpsCoordinates[3] = {
        0, 0, 0};
    base->Set(ANDROID_JPEG_GPS_COORDINATES, gpsCoordinates, 3);

    static const uint8_t gpsProcessingMethod[32] = "None";
    base->Set(ANDROID_JPEG_GPS_PROCESSING_METHOD, gpsProcessingMethod, 32);

    static const int64_t gpsTimestamp = 0;
    base->Set(ANDROID_JPEG_GPS_TIMESTAMP, &gpsTimestamp, 1);

    static const int32_t jpegOrientation = mSensorData.orientation;
    base->Set(ANDROID_JPEG_ORIENTATION, &jpegOrientation, 1);

    /** android.stats */

    static const uint8_t histogramMode = ANDROID_STATISTICS_HISTOGRAM_MODE_OFF;
    base->Set(ANDROID_STATISTICS_HISTOGRAM_MODE, &histogramMode, 1);

    static const uint8_t sharpnessMapMode = ANDROID_STATISTICS_HISTOGRAM_MODE_OFF;
    base->Set(ANDROID_STATISTICS_SHARPNESS_MAP_MODE, &sharpnessMapMode, 1);

    /** android.control */

    uint8_t controlIntent = 0;
    switch (type) {
        case RequestTemplate::kPreview:
            controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
            break;
        case RequestTemplate::kStillCapture:
            controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE;
            break;
        case RequestTemplate::kVideoRecord:
            controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD;
            break;
        case RequestTemplate::kVideoSnapshot:
            controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT;
            break;
        case RequestTemplate::kZeroShutterLag:
            controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_ZERO_SHUTTER_LAG;
            break;
        case RequestTemplate::kManual:
            controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_MANUAL;
            break;
        default:
            controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_CUSTOM;
            break;
    }
    base->Set(ANDROID_CONTROL_CAPTURE_INTENT, &controlIntent, 1);

    uint8_t controlMode = ANDROID_CONTROL_MODE_AUTO;
    uint8_t aeMode = ANDROID_CONTROL_AE_MODE_ON;
    uint8_t awbMode = ANDROID_CONTROL_AWB_MODE_AUTO;
    uint8_t faceDetectMode = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;

    static const uint8_t effectMode = ANDROID_CONTROL_EFFECT_MODE_OFF;
    base->Set(ANDROID_CONTROL_EFFECT_MODE, &effectMode, 1);

    static const uint8_t sceneMode = ANDROID_CONTROL_SCENE_MODE_DISABLED;
    base->Set(ANDROID_CONTROL_SCENE_MODE, &sceneMode, 1);

    static const int32_t aeExpCompensation = 0;
    base->Set(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &aeExpCompensation, 1);

    static const int32_t aeTargetFpsRange[2] = {
        15, 30};
    base->Set(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, aeTargetFpsRange, 2);

    static const uint8_t aeAntibandingMode =
        ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
    base->Set(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &aeAntibandingMode, 1);

    uint8_t afMode = 0;
    switch (type) {
        case RequestTemplate::kPreview:
            afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
            break;
        case RequestTemplate::kStillCapture:
            afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
            break;
        case RequestTemplate::kVideoRecord:
            afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
            break;
        case RequestTemplate::kVideoSnapshot:
            afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
            break;
        case RequestTemplate::kZeroShutterLag:
            afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
            break;
        case RequestTemplate::kManual:
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

    base->Set(ANDROID_CONTROL_AF_MODE, &afMode, 1);
    base->Set(ANDROID_CONTROL_MODE, &controlMode, 1);
    base->Set(ANDROID_CONTROL_AE_MODE, &aeMode, 1);
    base->Set(ANDROID_CONTROL_AWB_MODE, &awbMode, 1);
    base->Set(ANDROID_STATISTICS_FACE_DETECT_MODE, &faceDetectMode, 1);

    static const uint8_t aePrecaptureTrigger = ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;
    base->Set(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, &aePrecaptureTrigger, 1);

    static const uint8_t aeLock = ANDROID_CONTROL_AE_LOCK_OFF;
    base->Set(ANDROID_CONTROL_AE_LOCK, &aeLock, 1);

    static const uint8_t awbLock = ANDROID_CONTROL_AWB_LOCK_OFF;
    base->Set(ANDROID_CONTROL_AWB_LOCK, &awbLock, 1);

    static const uint8_t afTrigger = ANDROID_CONTROL_AF_TRIGGER_IDLE;
    base->Set(ANDROID_CONTROL_AF_TRIGGER, &afTrigger, 1);

    static const uint8_t flashState = ANDROID_FLASH_STATE_UNAVAILABLE;
    base->Set(ANDROID_FLASH_STATE, &flashState, 1);

    static const uint8_t pipelineDepth = 3;
    base->Set(ANDROID_REQUEST_PIPELINE_DEPTH, &pipelineDepth, 1);

    static const float colorGains[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    base->Set(ANDROID_COLOR_CORRECTION_GAINS, colorGains, ARRAY_SIZE(colorGains));

    static const camera_metadata_rational_t colorTransform[9] = {
        {1, 1}, {0, 1}, {0, 1}, {0, 1}, {1, 1}, {0, 1}, {0, 1}, {0, 1}, {1, 1}};
    base->Set(ANDROID_COLOR_CORRECTION_TRANSFORM, colorTransform, ARRAY_SIZE(colorTransform));

    if (strstr(mSensorData.camera_name, ISP_SENSOR_NAME)) {
        uint8_t hot_pixel_map_mode = ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF;
        base->Set(ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE, &hot_pixel_map_mode, 1);

        // In CameraDeviceTest.java, request the value is 100.
        // private static final int DEFAULT_POST_RAW_SENSITIVITY_BOOST = 100;
        int32_t sensitivity_boost = 100;
        base->Set(ANDROID_CONTROL_POST_RAW_SENSITIVITY_BOOST, &sensitivity_boost, 1);

        // Hardware level at least "limited", need fps fixed.
        static const int32_t aeTargetFpsRange[2] = {60, 60};
        base->Set(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, aeTargetFpsRange, 2);
    }

    return OK;
}

status_t CameraMetadata::setTemplate(CameraSensorMetadata mSensorData)
{
    //status_t res;

    for (uint32_t i = (uint32_t)RequestTemplate::kPreview;
         i <= (uint32_t)RequestTemplate::kManual;
         i++) {
        m_template_meta[i] = HalCameraMetadata::Create(1, 10);
        createSettingTemplate(m_template_meta[i], RequestTemplate(i),mSensorData);
    }

    return OK;
}

status_t CameraMetadata::getRequestSettings(RequestTemplate type,
                                            std::unique_ptr<HalCameraMetadata>* default_settings)
{
    if ((type < RequestTemplate::kPreview) ||
        (type > RequestTemplate::kManual) || (default_settings == NULL)) {
        ALOGE("%s: bad para, type %d, default_settings %p", __func__, (int)type, default_settings);
        return BAD_VALUE;
    }

    *default_settings =
        HalCameraMetadata::Clone(m_template_meta[(uint32_t)type].get());

    return OK;
}

#define META_CHECK(meta) \
do {\
    if((meta == NULL) || (meta->GetRawCameraMetadata() == NULL)) { \
        ALOGE("%s: meta %p", __func__, meta); \
        return BAD_VALUE; \
    }\
} while(0)

status_t CameraMetadata::Get(uint32_t tag,
                              camera_metadata_ro_entry* entry) const {
    if (entry == nullptr) {
        ALOGE("%s: entry is nullptr", __FUNCTION__);
        return BAD_VALUE;
    }

    std::unique_lock<std::mutex> lock(metadata_lock_);

    META_CHECK(m_request_meta);

    status_t ret = m_request_meta->Get(tag, entry);
    if (ret == NAME_NOT_FOUND) {
        ALOGE("%s: error reading tag", __func__);
        return BAD_VALUE;
    }

    return NO_ERROR;
}

int32_t CameraMetadata::getGpsCoordinates(double *pCoords, int count)
{
    status_t ret;
    camera_metadata_ro_entry entry;

    META_CHECK(m_request_meta);

    ret = m_request_meta->Get(ANDROID_JPEG_GPS_COORDINATES, &entry);
    if (ret == NAME_NOT_FOUND) {
        ALOGE("%s: error reading ANDROID_JPEG_GPS_COORDINATES", __func__);
        return BAD_VALUE;
    }

    for (int i=0; i<(int)entry.count && i<count; i++) {
        pCoords[i] = entry.data.d[i];
    }

    return NO_ERROR;
}

int32_t CameraMetadata::getGpsTimeStamp(int64_t &timeStamp)
{
    status_t ret;
    camera_metadata_ro_entry entry;

    META_CHECK(m_request_meta);

    ret = m_request_meta->Get(ANDROID_JPEG_GPS_TIMESTAMP, &entry);
    if (ret == NAME_NOT_FOUND) {
        ALOGE("%s: error reading ANDROID_JPEG_GPS_TIMESTAMP", __func__);
        return BAD_VALUE;
    }

    timeStamp = entry.data.i64[0];
    return NO_ERROR;
}

int32_t CameraMetadata::getGpsProcessingMethod(uint8_t* src, int count)
{
    status_t ret;
    camera_metadata_ro_entry entry;

    META_CHECK(m_request_meta);

    ret = m_request_meta->Get(ANDROID_JPEG_GPS_PROCESSING_METHOD, &entry);
    if (ret == NAME_NOT_FOUND) {
        ALOGE("%s: error reading ANDROID_JPEG_GPS_PROCESSING_METHOD", __func__);
        return BAD_VALUE;
    }

    int i;
    for (i=0; i<(int)entry.count && i<count-1; i++) {
        src[i] = entry.data.u8[i];
    }
    src[i] = '\0';

    return NO_ERROR;
}

int32_t CameraMetadata::getJpegRotation(int32_t &jpegRotation)
{
    status_t ret;
    camera_metadata_ro_entry entry;

    META_CHECK(m_request_meta);

    ret = m_request_meta->Get(ANDROID_JPEG_ORIENTATION, &entry);
    if (ret == NAME_NOT_FOUND) {
        ALOGE("%s: error reading ANDROID_JPEG_QUALITY", __func__);
        return BAD_VALUE;
    }

    jpegRotation = entry.data.i32[0];
    return NO_ERROR;
}

int32_t CameraMetadata::getJpegQuality(int32_t &quality)
{
    uint8_t u8Quality = 0;
    status_t ret;
    camera_metadata_ro_entry entry;

    META_CHECK(m_request_meta);

    ret = m_request_meta->Get(ANDROID_JPEG_QUALITY, &entry);
    if (ret == NAME_NOT_FOUND) {
        ALOGE("%s: error reading ANDROID_JPEG_QUALITY", __func__);
        return BAD_VALUE;
    }

    //4.3 framework change quality type from i32 to u8
    u8Quality = entry.data.u8[0];
    quality = u8Quality;

    return NO_ERROR;
}

int32_t CameraMetadata::getJpegThumbQuality(int32_t &thumb)
{
    uint8_t u8Quality = 0;
    status_t ret;
    camera_metadata_ro_entry entry;

    META_CHECK(m_request_meta);

    ret = m_request_meta->Get(ANDROID_JPEG_THUMBNAIL_QUALITY, &entry);
    if (ret == NAME_NOT_FOUND) {
        ALOGE("%s: error reading ANDROID_JPEG_THUMBNAIL_QUALITY", __func__);
        return BAD_VALUE;
    }

    //4.3 framework change quality type from i32 to u8
    u8Quality = entry.data.u8[0];
    thumb = u8Quality;

    return NO_ERROR;
}

int32_t CameraMetadata::getJpegThumbSize(int &width, int &height)
{
    status_t ret;
    camera_metadata_ro_entry entry;

    META_CHECK(m_request_meta);

    ret = m_request_meta->Get(ANDROID_JPEG_THUMBNAIL_SIZE, &entry);
    if (ret == NAME_NOT_FOUND) {
        ALOGE("%s: error reading ANDROID_JPEG_THUMBNAIL_SIZE", __func__);
        return BAD_VALUE;
    }

    width = entry.data.i32[0];
    height = entry.data.i32[1];
    return NO_ERROR;
}

int32_t CameraMetadata::getMaxJpegSize(int &size)
{
    status_t ret;
    camera_metadata_ro_entry entry;

    META_CHECK(m_request_meta);

    ret = m_request_meta->Get(ANDROID_JPEG_MAX_SIZE, &entry);
    if (ret == NAME_NOT_FOUND) {
        ALOGE("%s: error reading ANDROID_JPEG_MAX_SIZE", __func__);
        return BAD_VALUE;
    }

    size = entry.data.i32[0];
    return NO_ERROR;
}

int32_t CameraMetadata::getFocalLength(float &focalLength)
{
    status_t ret;
    camera_metadata_ro_entry entry;

    META_CHECK(m_request_meta);

    ret = m_request_meta->Get(ANDROID_LENS_FOCAL_LENGTH, &entry);
    if (ret == NAME_NOT_FOUND) {
        ALOGE("%s: error reading ANDROID_LENS_FOCAL_LENGTH", __func__);
        return BAD_VALUE;
    }

    focalLength = entry.data.f[0];

    return 0;
}


}  // namespace android
