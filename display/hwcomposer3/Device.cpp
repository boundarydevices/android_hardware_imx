/*
 * Copyright (C) 2022 The Android Open Source Project
 * Copyright 2023 NXP
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

#include "Device.h"

#include <android-base/file.h>
#include <android-base/properties.h>
#include <json/json.h>

#include "ClientFrameComposer.h"
#include "FrameComposer.h"

ANDROID_SINGLETON_STATIC_INSTANCE(aidl::android::hardware::graphics::composer3::impl::Device);

namespace aidl::android::hardware::graphics::composer3::impl {
namespace {

std::string getPmemPath() {
    return ::android::base::GetProperty("ro.vendor.hwcomposer.pmem", "");
}

HWC3::Error loadPersistentKeyValues(Json::Value* dictionary) {
    *dictionary = Json::Value(Json::ValueType::objectValue);

    const std::string path = getPmemPath();
    if (path.empty()) {
        ALOGE("%s: persistent key-value store path not available.", __FUNCTION__);
        return HWC3::Error::NoResources;
    }

    std::string content;
    if (!::android::base::ReadFileToString(path, &content)) {
        ALOGE("%s: failed to read key-value store from %s", __FUNCTION__, path.c_str());
        return HWC3::Error::NoResources;
    }

    if (content.empty() || content[0] == '\0') {
        return HWC3::Error::None;
    }

    Json::Reader reader;
    if (!reader.parse(content, *dictionary)) {
        const std::string error = reader.getFormattedErrorMessages();
        ALOGE("%s: failed to parse key-value store from %s:%s", __FUNCTION__, path.c_str(),
              error.c_str());
        return HWC3::Error::NoResources;
    }

    return HWC3::Error::None;
}

HWC3::Error savePersistentKeyValues(const Json::Value& dictionary) {
    const std::string path = getPmemPath();
    if (path.empty()) {
        ALOGE("%s: persistent key-value store path not available.", __FUNCTION__);
        return HWC3::Error::NoResources;
    }

    const std::string contents = dictionary.toStyledString();
    if (!::android::base::WriteStringToFile(contents, path)) {
        ALOGE("%s: failed to write key-value store to %s", __FUNCTION__, path.c_str());
        return HWC3::Error::NoResources;
    }

    return HWC3::Error::None;
}

} // namespace

HWC3::Error Device::getComposer(FrameComposer** outComposer) {
    std::unique_lock<std::mutex> lock(mMutex);

    if (mComposer == nullptr) {
        mComposer = std::make_unique<ClientFrameComposer>();

        if (!mComposer) {
            ALOGE("%s failed to allocate FrameComposer", __FUNCTION__);
            return HWC3::Error::NoResources;
        }

        HWC3::Error error = mComposer->init();
        if (error != HWC3::Error::None) {
            ALOGE("%s failed to init FrameComposer", __FUNCTION__);
            return error;
        }
    }

    mComposerMutex.lock();
    *outComposer = mComposer.get();
    return HWC3::Error::None;
}

void Device::releaseComposer() {
    mComposerMutex.unlock();
}

HWC3::Error Device::getPersistentKeyValue(const std::string& key, const std::string& defaultValue,
                                          std::string* outValue) {
    std::unique_lock<std::mutex> lock(mMutex);

    Json::Value dictionary;

    HWC3::Error error = loadPersistentKeyValues(&dictionary);
    if (error != HWC3::Error::None) {
        ALOGE("%s: failed to load pmem json", __FUNCTION__);
        return error;
    }

    if (!dictionary.isMember(key)) {
        *outValue = defaultValue;
        return HWC3::Error::None;
    }

    *outValue = defaultValue;

    return HWC3::Error::None;
}

HWC3::Error Device::setPersistentKeyValue(const std::string& key, const std::string& value) {
    std::unique_lock<std::mutex> lock(mMutex);

    Json::Value dictionary;

    HWC3::Error error = loadPersistentKeyValues(&dictionary);
    if (error != HWC3::Error::None) {
        ALOGE("%s: failed to load pmem json", __FUNCTION__);
        return error;
    }

    dictionary[key] = value;

    error = savePersistentKeyValues(dictionary);
    if (error != HWC3::Error::None) {
        ALOGE("%s: failed to save pmem json", __FUNCTION__);
        return error;
    }

    return HWC3::Error::None;
}

} // namespace aidl::android::hardware::graphics::composer3::impl
