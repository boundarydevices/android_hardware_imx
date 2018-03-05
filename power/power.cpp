/*
 * Copyright (C) 2012 The Android Open Source Project
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
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
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#define LOG_TAG "i.MXPowerHAL"
#include <utils/Log.h>

#include <hardware/hardware.h>
#include <hardware/power.h>
#include <utils/StrongPointer.h>
#include <cutils/properties.h>

#define POLICY_PATH "/sys/devices/system/cpu/cpufreq"
#define GOVERNOR_PATH_FMT "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor"
#define BOOSTPULSE_PATH "/sys/devices/system/cpu/cpufreq/interactive/boostpulse"
#define PROP_CPUFREQGOV "sys.interactive"
#define PROP_VAL "active"

#define CONSERVATIVE "conservative"
#define INTERACTIVE  "interactive"
#define POWERSAVE    "powersave"
#define PERFORMANCE  "performance"

#define MAX_POLICY_NUM 2
static int interactive_mode = 0;
static int policy_cpu[MAX_POLICY_NUM] = {0}; /*only support up to two policy*/
static int policy_num = 0;

static void sysfs_write(const char *path, const char *s)
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

static ssize_t sysfs_read(char *path, char *s, int num_bytes)
{
    ssize_t n;
    int fd = open(path, O_RDONLY);

    if (fd < 0) {
        ALOGE("Error reading from %s: %s\n", path, strerror(errno));
        return -1;
    }

    if ((n = read(fd, s, (num_bytes - 1))) < 0) {
        ALOGE("Error reading from  %s: %s\n", path, strerror(errno));
    } else {
        if ((n >= 1) && (s[n-1] == '\n')) {
            s[n-1] = '\0';
        } else {
            s[n] = '\0';
        }
    }

    close(fd);
    ALOGV("read '%s' from %s", s, path);

    return n;
}
int do_setproperty(const char *propname, const char *propval)
{
    char prop_cpugov[PROPERTY_VALUE_MAX];
    int ret;

    property_set(propname, propval);
    if ( property_get(propname, prop_cpugov, NULL) &&
                     (strcmp(prop_cpugov, propval) == 0) ) {
	ret = 0;
        ALOGV("setprop: %s = %s ", propname, propval);
    } else {
        ret = -1;
        ALOGE("setprop: %s = %s fail\n", propname, propval);
    }
    return ret;
}

int do_changecpugov(const char *gov)
{
    int fd;
    char governor_path[256];
    for (int i=0; i<policy_num; i++) {
        sprintf(governor_path, GOVERNOR_PATH_FMT, policy_cpu[i]);
        fd = open(governor_path, O_WRONLY);
        if (fd < 0) {
            ALOGE("Error opening %s: %s\n", governor_path, strerror(errno));
            return -1;
        }
        if (write(fd, gov, strlen(gov)) < 0) {
            ALOGE("Error writing to %s: %s\n", governor_path, strerror(errno));
            close(fd);
            return -1;
        }
    }

    if (strncmp(INTERACTIVE, gov, strlen(INTERACTIVE)) == 0) {
        do_setproperty(PROP_CPUFREQGOV, PROP_VAL);
        interactive_mode = 1;
    } else {
        interactive_mode = 0;
    }

    close(fd);
    return 0;
}

static void fsl_power_init(struct power_module *module)
{
    /*
     * cpufreq interactive governor: timer 40ms, min sample 60ms,
     * hispeed at cpufreq MAX freq in freq_table at load 40% 
     * the params is initialized in init.rc
     */
    (void)module;

    dirent* de;
    int fd;
    int dfd;
    DIR* d = opendir(POLICY_PATH);
    if (d) {
        dfd = dirfd(d);
        while ((de = readdir(d)) != nullptr) {
            if (de->d_type != DT_DIR || de->d_name[0] == '.') continue;

            fd = openat(dfd, de->d_name, O_RDONLY | O_DIRECTORY);
            if (fd < 0) continue;

            if (strncmp(de->d_name, "policy", 5) == 0) {
                sscanf(de->d_name, "policy%d", &policy_cpu[policy_num]);
                policy_num++;
                if (policy_num >= MAX_POLICY_NUM)
                    break;
            }
        }
    }

    do_changecpugov(INTERACTIVE);
}

static void fsl_power_set_interactive(struct power_module *module, int on)
{
    /* swich to conservative when system in early_suspend or
     * suspend mode.
     */
    (void)module;

    if (on)
        do_changecpugov(INTERACTIVE);
    else
        do_changecpugov(CONSERVATIVE);
}

static void fsl_power_hint(struct power_module *module, power_hint_t hint,
                            void *data)
{
    (void)module;

    switch (hint) {
    case POWER_HINT_VSYNC:
        break;
    case POWER_HINT_INTERACTION:
        if (interactive_mode)
            sysfs_write(BOOSTPULSE_PATH, "1");
        else
            do_changecpugov(INTERACTIVE);
        break;
    case POWER_HINT_LOW_POWER:
        if (data)
            do_changecpugov(POWERSAVE);
        else
            do_changecpugov(INTERACTIVE);
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
        .dso = NULL,
        .reserved = {0}
    },

    .init = fsl_power_init,
    .setInteractive = fsl_power_set_interactive,
    .powerHint = fsl_power_hint,
};
