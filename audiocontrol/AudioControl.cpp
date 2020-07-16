//
// Copyright (C) 2017 The Android Open Source Project
// Copyright 2019 NXP
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "AudioControl.h"

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <android-base/stringprintf.h>
#include <android-base/file.h>
#include <android-base/unique_fd.h>

#include <hidl/HidlTransportSupport.h>
#include <log/log.h>


namespace android {
namespace hardware {
namespace automotive {
namespace audiocontrol {
namespace V1_0 {
namespace implementation {

using ::android::base::EqualsIgnoreCase;
using ::android::base::StringPrintf;
using ::android::base::WriteStringToFd;

// This is the static map we're using to associate a ContextNumber with a
// bus number from the audio_policy_configuration.xml setup.  Every valid context needs
// to be mapped to a bus address that actually exists in the platforms configuration.
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))  // Would be nice if this were common...
static int sContextToBusMap[] = {
    -1,     // INVALID
     0,     // MUSIC_CONTEXT
     0,     // NAVIGATION_CONTEXT
     0,     // VOICE_COMMAND_CONTEXT
     0,     // CALL_RING_CONTEXT
     0,     // CALL_CONTEXT
     1,     // ALARM_CONTEXT
     1,     // NOTIFICATION_CONTEXT
     1,     // SYSTEM_SOUND_CONTEXT
};
static const unsigned sContextMapSize = ARRAY_SIZE(sContextToBusMap);
static const unsigned sContextCount = sContextMapSize - 1;  // Less one for the INVALID entry
static const unsigned sContextNumberMax = sContextCount;    // contextNumber is counted from 1


AudioControl::AudioControl() {
};


Return<int32_t> AudioControl::getBusForContext(ContextNumber ctxt) {
    unsigned contextNumber = static_cast<unsigned>(ctxt);
    if (contextNumber > sContextNumberMax) {
        ALOGE("Unexpected context number %d (max expected is %d)", contextNumber, sContextCount);
        return -1;
    } else {
        return sContextToBusMap[contextNumber];
    }
}


Return<void> AudioControl::setBalanceTowardRight(float value) {
    // For completeness, lets bounds check the input...
    if ((value > 1.0f) || (value < -1.0f)) {
        ALOGE("Balance value out of range -1 to 1 at %0.2f", value);
    } else {
        // Just log in this default mock implementation
        ALOGI("Balance set to %0.2f", value);
    }
    return Void();
}


Return<void> AudioControl::setFadeTowardFront(float value) {
    // For completeness, lets bounds check the input...
    if ((value > 1.0f) || (value < -1.0f)) {
        ALOGE("Fader value out of range -1 to 1 at %0.2f", value);
    } else {
        // Just log in this default mock implementation
        ALOGI("Fader set to %0.2f", value);
    }
    return Void();
}

Return<void> AudioControl::debug(const hidl_handle& fd , const hidl_vec<hidl_string>& options) {
    if (fd.getNativeHandle() != nullptr && fd->numFds > 0) {
        cmdDump(fd->data[0], options);
    } else {
        LOG(ERROR) << "Given file descriptor is not valid.";
    }

    return {};
}

void AudioControl::cmdDump(int fd, const hidl_vec<hidl_string>& options) {
    if (options.size() == 0) {
        WriteStringToFd("No option is given.\n", fd);
        cmdHelp(fd);
        return;
    }

    const std::string option = options[0];
    if (EqualsIgnoreCase(option, "--help")) {
        cmdHelp(fd);
    } else if (EqualsIgnoreCase(option, "--list")) {
        cmdList(fd, options);
    } else if (EqualsIgnoreCase(option, "--dump")) {
        cmdDumpDevice(fd, options);
    } else {
        WriteStringToFd(StringPrintf("Invalid option: %s\n", option.c_str()),fd);
        cmdHelp(fd);
    }
}

void AudioControl::cmdHelp(int fd) {
    WriteStringToFd("--help: shows this help.\n"
                    "--list: [option1|option2|...|all]: lists all the dump options: option1 or option2 or ... or all\n"
                    "available to AudioControl Hal.\n"
                    "--dump option1: shows current status of the option1\n"
                    "--dump option2: shows current status of the option2\n"
                    "--dump all: shows current status of all the options\n", fd);
    return;
}

void AudioControl::cmdList(int fd, const hidl_vec<hidl_string>& options) {
    bool listoption1 = false;
    bool listoption2 = false;
    if (options.size() > 1) {
        const std::string option = options[1];
        const bool listAll = EqualsIgnoreCase(option, "all");
        listoption1 = listAll || EqualsIgnoreCase(option, "option1");
        listoption2 = listAll || EqualsIgnoreCase(option, "option2");
        if (!listoption1 && !listoption2) {
            WriteStringToFd(StringPrintf("Unrecognized option is ignored.\n\n"),fd);
            cmdHelp(fd);
            return;
        }
        if(listoption1) {
            WriteStringToFd(StringPrintf("list option1 dump options, default is --list listoption1.\n"),fd);
         }

        if(listoption2) {
            WriteStringToFd(StringPrintf("list option2 dump options, default is --list listoption2.\n"),fd);
        }
    } else {
        WriteStringToFd(StringPrintf("Invalid input, need to append list option.\n\n"),fd);
        cmdHelp(fd);
     }
}

void AudioControl::cmdDumpDevice(int fd, const hidl_vec<hidl_string>& options) {
    bool listoption1 = false;
    bool listoption2 = false;
    if (options.size() > 1) {
        const std::string option = options[1];
        const bool listAll = EqualsIgnoreCase(option, "all");
        listoption1 = listAll || EqualsIgnoreCase(option, "option1");
        listoption2 = listAll || EqualsIgnoreCase(option, "option2");
        if (!listoption1 && !listoption2) {
            WriteStringToFd(StringPrintf("Unrecognized option is ignored.\n\n"),fd);
            cmdHelp(fd);
            return;
        }
        if(listoption1) {
            WriteStringToFd(StringPrintf("dump option1 info.\n"),fd);
        }
        if(listoption2) {
            WriteStringToFd(StringPrintf("dump option2 info.\n"),fd);
        }
    } else {
        WriteStringToFd(StringPrintf("Invalid input, need to append dump option.\n\n"),fd);
        cmdHelp(fd);
    }
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace audiocontrol
}  // namespace automotive
}  // namespace hardware
}  // namespace android
