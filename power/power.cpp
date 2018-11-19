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
#include <unistd.h>
#include <stdio.h>
#define LOG_TAG "i.MXPowerHAL"
#include <utils/Log.h>

#include <hardware/hardware.h>
#include <hardware/power.h>
#include <utils/StrongPointer.h>
#include <cutils/properties.h>

#define POLICY_PATH "/sys/devices/system/cpu/cpufreq"
#define BOOSTPULSE_PATH "/sys/devices/system/cpu/cpufreq/interactive/boostpulse"

#define MAX_POLICY_NUM 2
#define MAX_FREQ_LEN   16

constexpr char kPowerHalStateProp[] = "vendor.powerhal.state";
constexpr char kPowerHalMaxFreqProp[] = "vendor.powerhal.lowpower.max_freq";
static int policy_num = 0;

struct power_hal_t {
    int policy_cpu;
    char max_cpufreq[MAX_FREQ_LEN];
    char min_cpufreq[MAX_FREQ_LEN];
}imx_power[MAX_POLICY_NUM] = {0};

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
    char prop[PROPERTY_VALUE_MAX];
    int ret;

    property_set(propname, propval);
    if ( property_get(propname, prop, NULL) &&
                     (strcmp(prop, propval) == 0) ) {
	ret = 0;
        ALOGV("setprop: %s = %s ", propname, propval);
    } else {
        ret = -1;
        ALOGE("setprop: %s = %s fail\n", propname, propval);
    }
    return ret;
}

static int cpufreq_set_max_frequency_limit(int low_power)
{
    int fd;
    char path[256];
    char *param;
    char prop[PROPERTY_VALUE_MAX];
    for (int i=0; i<policy_num; i++) {
        sprintf(path, POLICY_PATH"policy%d/scaling_max_freq", imx_power[i].policy_cpu);
        fd = open(path, O_WRONLY);
        if (fd < 0) {
            ALOGE("Error opening %s: %s\n", path, strerror(errno));
            return -1;
        }

        if (low_power) {
            if (property_get(kPowerHalMaxFreqProp, prop, NULL))
                param = prop;
            else
                param = imx_power[i].min_cpufreq;
        } else {
            param = imx_power[i].max_cpufreq;
        }
        if (write(fd, param, strlen(param)) < 0) {
            ALOGE("Error writing to %s: %s\n", path, strerror(errno));
            close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

static void fsl_power_init(struct power_module *module)
{
    (void)module;
    char path[256];
    char *param;
    int cpu_id;
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

            if (strncmp(de->d_name, "policy", 6) == 0) {
                sscanf(de->d_name, "policy%d", &cpu_id);
                imx_power[policy_num].policy_cpu = cpu_id;
                sprintf(path, POLICY_PATH"/policy%d/cpuinfo_max_freq", cpu_id);
                param = imx_power[policy_num].max_cpufreq;
                if (sysfs_read(path, param, MAX_FREQ_LEN) < 0)
                    imx_power[policy_num].max_cpufreq[0] = '\0';

                sprintf(path, POLICY_PATH"/policy%d/cpuinfo_min_freq", cpu_id);
                param = imx_power[policy_num].min_cpufreq;
                if (sysfs_read(path, param, MAX_FREQ_LEN) < 0)
                    imx_power[policy_num].min_cpufreq[0] = '\0';

                policy_num++;
                if (policy_num >= MAX_POLICY_NUM)
                    break;
            }
        }
    }
}

static void fsl_power_set_interactive(struct power_module *module, int on)
{
    (void)module;
    (void)on;
}

static void fsl_power_hint(struct power_module *module, power_hint_t hint,
                            void *data)
{
    (void)module;
    switch (hint) {
    case POWER_HINT_VSYNC:
        break;
    case POWER_HINT_INTERACTION:
        do_setproperty(kPowerHalStateProp, "interaction");
        break;
    case POWER_HINT_LOW_POWER:
        if (data) {
            cpufreq_set_max_frequency_limit(1);
            do_setproperty(kPowerHalStateProp, "low_power_on");
        } else {
            cpufreq_set_max_frequency_limit(0);
            do_setproperty(kPowerHalStateProp, "low_power_off");
        }
        break;
    case POWER_HINT_SUSTAINED_PERFORMANCE:
        if (data) {
            do_setproperty(kPowerHalStateProp, "sustained_perf_on");
        } else {
            do_setproperty(kPowerHalStateProp, "sustained_perf_off");
        }
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
