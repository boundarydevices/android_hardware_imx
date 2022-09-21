#ifndef VENDOR_TAGS_H
#define VENDOR_TAGS_H

#include <hal_types.h>

namespace android {

using google_camera_hal::CameraMetadataType;
using google_camera_hal::VendorTagSection;

static const uint32_t kImxTagIdOffset = VENDOR_SECTION_START;

typedef enum enumImxTag {
    VSI_DEWARP = kImxTagIdOffset,
    VSI_HFLIP,
    VSI_VFLIP,
    VSI_LSC,
    VSI_BRIGHTNESS,
    VSI_CONTRAST,
    VSI_SATURATION,
    VSI_HUE,
    VSI_SHARP_LEVEL,
    VSI_EXPOSURE_GAIN
} ImxTag;

static const std::vector<VendorTagSection> kImxTagSections = {
    {
        .section_name = "vsi",
        .tags =
            {
                {
                    .tag_id = VSI_DEWARP,
                    .tag_name = "dewarp.enable",
                    .tag_type = CameraMetadataType::kInt32,
                },
                {
                    .tag_id = VSI_HFLIP,
                    .tag_name = "hflip.enable",
                    .tag_type = CameraMetadataType::kInt32,
                },
                {
                    .tag_id = VSI_VFLIP,
                    .tag_name = "vflip.enable",
                    .tag_type = CameraMetadataType::kInt32,
                },
                {
                    .tag_id = VSI_LSC,
                    .tag_name = "lsc.enable",
                    .tag_type = CameraMetadataType::kInt32,
                },
                {
                    .tag_id = VSI_BRIGHTNESS,
                    .tag_name = "brightness",
                    .tag_type = CameraMetadataType::kInt32,
                },
                {
                    .tag_id = VSI_CONTRAST,
                    .tag_name = "contrast",
                    .tag_type = CameraMetadataType::kFloat,
                },
                {
                    .tag_id = VSI_SATURATION,
                    .tag_name = "saturation",
                    .tag_type = CameraMetadataType::kFloat,
                },
                {
                    .tag_id = VSI_HUE,
                    .tag_name = "hue",
                    .tag_type = CameraMetadataType::kInt32,
                },
                {
                    .tag_id = VSI_SHARP_LEVEL,
                    .tag_name = "sharp.level",
                    .tag_type = CameraMetadataType::kByte,
                },
                {
                    .tag_id = VSI_EXPOSURE_GAIN,
                    .tag_name = "exposure.gain",
                    .tag_type = CameraMetadataType::kInt32,
                },
            }
    }
};

}  // namespace android

#endif // VENDOR_TAGS_H
