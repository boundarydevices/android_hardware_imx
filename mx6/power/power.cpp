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
#include <utils/StrongPointer.h>

#include <cutils/properties.h>
#include "watchdog.h"

#define BOOST_PATH      "/sys/devices/system/cpu/cpufreq/interactive/boost"
#define BOOSTPULSE_PATH "/sys/devices/system/cpu/cpufreq/interactive/boostpulse"
#define INPUTBOOST_PATH "/sys/devices/system/cpu/cpufreq/interactive/input_boost"
#define GOV_PATH "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
#define PROP_CPUFREQGOVERNOR "sys.interactive"
#define PROP_VAL "active"
#define CONSERVATIVE "conservative"
static int boost_fd = -1;
static int boost_warned;
static sp<WatchdogThread> wdThread;

static void getcpu_gov(char *s, int size)
{
    int len;
    int fd = open(GOV_PATH, O_RDONLY);

    if (fd < 0) {
        ALOGE("Error opening %s: %s\n", GOV_PATH, strerror(errno));
        return;
    }

    len = read(fd, s, size);
    if (len < 0) {
        ALOGE("Error read %s: %s\n", GOV_PATH, strerror(errno));
    }
    close(fd);
}

static int check_and_set_property(const char *propname, const char *propval)
{
    char prop_cpugov[PROPERTY_VALUE_MAX];
    char cpugov[64];
    int ret;
    memset(cpugov, 0, sizeof(cpugov));
    getcpu_gov(cpugov,sizeof(cpugov));

    property_set(propname, propval);
    if( property_get(propname, prop_cpugov, NULL) &&
            (strcmp(prop_cpugov, propval) == 0) ){
	    ret = 0;
    }else{
	    ret = -1;
	    ALOGE("setprop: %s = %s fail\n", propname, propval);
    }
    return ret;
}

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

static void fsl_power_init(struct power_module *module)
{
    /*
     * cpufreq interactive governor: timer 40ms, min sample 60ms,
     * hispeed at cpufreq MAX freq in freq_table at load 40% 
     * move the params initialization to init
     */
    check_and_set_property(PROP_CPUFREQGOVERNOR, PROP_VAL);
    // create and run the watchdog thread
    wdThread = new WatchdogThread();
}

static void fsl_power_set_interactive(struct power_module *module, int on)
{
    /* swich to conservative when system in early_suspend or
     * suspend mode.
	 */
    if (on){
        sysfs_write(GOV_PATH, "interactive");
		check_and_set_property(PROP_CPUFREQGOVERNOR, PROP_VAL);
		sysfs_write(INPUTBOOST_PATH, "1");
	}else{
        sysfs_write(INPUTBOOST_PATH, "0");
		sysfs_write(GOV_PATH, "conservative");
	}
}

static void fsl_power_hint(struct power_module *module, power_hint_t hint,
                            void *data)
{
    char buf[80];
    int len;
    memset(buf, 0, sizeof(buf));
    getcpu_gov(buf,sizeof(buf));
    if(!strncmp(CONSERVATIVE, buf, strlen(CONSERVATIVE))){
	   ALOGE("Not in interactive mode, don't do powerhint\n");
	    return;
    }
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
    open: NULL,
};

struct power_module HAL_MODULE_INFO_SYM = {
    common: {
        tag: HARDWARE_MODULE_TAG,
        module_api_version: POWER_MODULE_API_VERSION_0_2,
        hal_api_version: HARDWARE_HAL_API_VERSION,
        id: POWER_HARDWARE_MODULE_ID,
        name: "FSL i.MX Power HAL",
        author: "Freescale Semiconductor, Inc.",
        methods: &power_module_methods,
        dso: NULL,
        reserved: {0}
    },

    init: fsl_power_init,
    setInteractive: fsl_power_set_interactive,
    powerHint: fsl_power_hint,
};
