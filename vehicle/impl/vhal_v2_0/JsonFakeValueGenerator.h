/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef android_hardware_automotive_vehicle_V2_0_impl_JsonFakeValueGenerator_H_
#define android_hardware_automotive_vehicle_V2_0_impl_JsonFakeValueGenerator_H_

#include <json/json.h>

#include <chrono>
#include <iostream>

#include "FakeValueGenerator.h"

namespace android {
namespace hardware {
namespace automotive {
namespace vehicle {
namespace V2_0 {

namespace impl {

class JsonFakeValueGenerator : public FakeValueGenerator {
private:
    struct GeneratorCfg {
        size_t index;
        std::vector<VehiclePropValue> events;
    };

public:
    JsonFakeValueGenerator(const VehiclePropValue& request);
    JsonFakeValueGenerator(std::string path);

    ~JsonFakeValueGenerator() = default;

    VehiclePropValue nextEvent();
    std::vector<VehiclePropValue> getAllEvents();

    bool hasNext();

private:
    std::vector<VehiclePropValue> parseFakeValueJson(std::istream& is);
    void copyMixedValueJson(VehiclePropValue::RawValue& dest, const Json::Value& jsonValue);

    template <typename T>
    void copyJsonArray(hidl_vec<T>& dest, const Json::Value& jsonArray);

    bool isDiagnosticProperty(int32_t prop);
    hidl_vec<uint8_t> generateDiagnosticBytes(const VehiclePropValue::RawValue& diagnosticValue);
    void setBit(hidl_vec<uint8_t>& bytes, size_t idx);

private:
    GeneratorCfg mGenCfg;
    int32_t mNumOfIterations;
};

} // namespace impl

} // namespace V2_0
} // namespace vehicle
} // namespace automotive
} // namespace hardware
} // namespace android

#endif // android_hardware_automotive_vehicle_V2_0_impl_JsonFakeValueGenerator_H_
