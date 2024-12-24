#include <string>
#include <unordered_map>
#include <unordered_set>

#include <aidl/android/media/audio/common/AudioHalCapCriterion.h>
#include <aidl/android/media/audio/common/AudioHalCapCriterionType.h>
#include <aidl/android/media/audio/common/AudioHalCapCriterionV2.h>
#include <aidl/android/media/audio/common/AudioHalVolumeCurve.h>
#include <aidl/android/media/audio/common/AudioPort.h>
#include <android_audio_policy_configuration.h>
#include <android_audio_policy_configuration_enums.h>
#include <android_audio_policy_engine_configuration.h>
#include <media/AidlConversionUtil.h>

#include "core-impl/Module.h"

namespace aidl::android::hardware::audio::core::internal {

namespace engineconfiguration = ::android::audio::policy::engine::configuration;
namespace aidlaudiocommon = ::aidl::android::media::audio::common;

static constexpr const char kXsdcForceConfigForUse[] = "ForceUseFor";

ConversionResult<aidlaudiocommon::AudioPolicyForceUse> convertForceUseCriterionToAidl(
        const std::string& xsdcCriterionName);
ConversionResult<aidlaudiocommon::AudioPolicyForcedConfig> convertForcedConfigToAidl(
        const std::string& xsdcForcedConfigCriterionType);
ConversionResult<aidlaudiocommon::AudioDeviceAddress> convertDeviceAddressToAidl(
        const std::string& xsdcAddress);
ConversionResult<aidlaudiocommon::AudioMode> convertTelephonyModeToAidl(
        const std::string& xsdcModeCriterionType);
ConversionResult<aidlaudiocommon::AudioDeviceDescription> convertDeviceTypeToAidl(
        const std::string& xType);
ConversionResult<std::vector<std::optional<aidlaudiocommon::AudioHalCapCriterionV2>>>
convertCapCriteriaCollectionToAidl(
        const std::vector<engineconfiguration::CriteriaType>& xsdcCriteriaVec,
        const std::vector<engineconfiguration::CriterionTypesType>& xsdcCriterionTypesVec);
ConversionResult<aidlaudiocommon::AudioHalCapCriterionV2> convertCapCriterionV2ToAidl(
        const engineconfiguration::CriterionType& xsdcCriterion,
        const std::vector<engineconfiguration::CriterionTypesType>& xsdcCriterionTypesVec);
ConversionResult<aidlaudiocommon::AudioHalVolumeCurve::CurvePoint> convertCurvePointToAidl(
        const std::string& xsdcCurvePoint);
ConversionResult<std::unique_ptr<Module::Configuration>> convertModuleConfigToAidl(
        const ::android::audio::policy::configuration::Modules::Module& moduleConfig);
ConversionResult<aidlaudiocommon::AudioUsage> convertAudioUsageToAidl(
        const engineconfiguration::UsageEnumType& xsdcUsage);
ConversionResult<aidlaudiocommon::AudioContentType> convertAudioContentTypeToAidl(
        const engineconfiguration::ContentType& xsdcContentType);
ConversionResult<aidlaudiocommon::AudioSource> convertAudioSourceToAidl(
        const engineconfiguration::SourceEnumType& xsdcSourceType);
ConversionResult<aidlaudiocommon::AudioStreamType> convertAudioStreamTypeToAidl(
        const engineconfiguration::Stream& xsdStreamType);
ConversionResult<int32_t> convertAudioFlagsToAidl(
        const std::vector<engineconfiguration::FlagType>& xsdcFlagTypeVec);
}  // namespace aidl::android::hardware::audio::core::internal
