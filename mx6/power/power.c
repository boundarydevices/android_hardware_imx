/*
 * Copyright (C) 2012 The Android Open Source Project
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
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
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define LOG_TAG "i.MXPowerHAL"
#include <utils/Log.h>

#include <hardware/hardware.h>
#include <hardware/power.h>

#define BOOST_PATH      "/sys/devices/system/cpu/cpufreq/interactive/boost"
#define BOOSTPULSE_PATH "/sys/devices/system/cpu/cpufreq/interactive/boostpulse"
static int boost_fd = -1;
static int boost_warned;

static void sysfs_write(char *path, char *s)
{
    int len;
    int fd = open(path, O_WRONLY);

    if (fd < 0) {
        ALOGE("Error opening %s: %s\n", path, strerror(errno));
        return;
    }

    len = write(fd, s, strlen(s));
    if (len < 0) {
        ALOGE("Error writing to %s: %s\n", path, strerror(errno));
    }

    close(fd);
}

static void fsl_power_init(struct power_module *module)
{
    /*
     * cpufreq interactive governor: timer 20ms, min sample 60ms,
     * hispeed at cpufreq MAX point at load 40%
     */

    sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/timer_rate",
                "20000");
    sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/min_sample_time",
                "60000");
    /*
     * use cpufreq max freq default, the high speed also can be specified by
     * wrriten a value to hispeed like below set high speed to 800MHz:
     * sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/hispeed_freq",
                "792000");
     */

    sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/go_hispeed_load",
                "40");
    sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/input_boost",
		"1");
}

static void fsl_power_set_interactive(struct power_module *module, int on)
{
	sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/input_boost",
			on ? "1" : "0");
}

static void fsl_power_hint(struct power_module *module, power_hint_t hint,
                            void *data)
{
    char buf[80];
    int len;

    switch (hint) {
    case POWER_HINT_VSYNC:
        break;
    case POWER_HINT_INTERACTION:
	sysfs_write(BOOSTPULSE_PATH, "1");
	break;
    default:
            break;
    }
}

static struct hw_module_methods_t power_module_methods = {
    .open = NULL,
};

struct power_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = POWER_MODULE_API_VERSION_0_2,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = POWER_HARDWARE_MODULE_ID,
        .name = "FSL i.MX Power HAL",
        .author = "Freescale Semiconductor, Inc.",
        .methods = &power_module_methods,
    },

    .init = fsl_power_init,
    .setInteractive = fsl_power_set_interactive,
    .powerHint = fsl_power_hint,
};
