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

#define LOG_TAG "i.MX5X PowerHAL"
#include <utils/Log.h>

#include <hardware/hardware.h>
#include <hardware/power.h>

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
	sysfs_write("/sys/devices/platform/imx_dvfscore.0/enable", "1");
}

static void fsl_power_set_interactive(struct power_module *module, int on)
{

}

static void fsl_power_hint(struct power_module *module, power_hint_t hint,
                            void *data)
{

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
        .name = "FSL i.MX5X Power HAL",
        .author = "Freescale Semiconductor, Inc.",
        .methods = &power_module_methods,
    },

    .init = fsl_power_init,
    .setInteractive = fsl_power_set_interactive,
    .powerHint = fsl_power_hint,
};
