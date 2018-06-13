/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright 2009-2016 Freescale Semiconductor, Inc.
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

#define LOG_TAG "lights"

#include <hardware/lights.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <cutils/log.h>
#include <cutils/atomic.h>
#include <cutils/properties.h>
#include <ctype.h>
#include <dirent.h>
#include <string.h>

#define MAX_BRIGHTNESS 255
#define DEF_BACKLIGHT_DEV "pwm-backlight"
#define DEF_BACKLIGHT_PATH "/sys/class/backlight/"

/*****************************************************************************/
struct lights_module_t {
    struct hw_module_t common;
};

static int lights_device_open(const struct hw_module_t* module,
                              const char* name, struct hw_device_t** device);

static struct hw_module_methods_t lights_module_methods = {
    .open= lights_device_open
};

struct lights_module_t HAL_MODULE_INFO_SYM = {
    .common= {
        .tag= HARDWARE_MODULE_TAG,
        .version_major= 1,
        .version_minor= 0,
        .id= LIGHTS_HARDWARE_MODULE_ID,
        .name= "Lights module",
        .author= "Freescale Semiconductor",
        .methods= &lights_module_methods,
    }
};

static char backlight_max_path[256], backlight_path[256];

#if BOARD_HAS_BATTERY_LIGHTS
char const*const RED_LED_FILE
        = "/sys/class/leds/red/brightness";

char const*const GREEN_LED_FILE
        = "/sys/class/leds/green/brightness";

char const*const BLUE_LED_FILE
        = "/sys/class/leds/blue/brightness";

static int rgb_brightness_ratio = 255;
#endif

// ****************************************************************************
// module
// ****************************************************************************
static int
write_int(char const* path, int value)
{
    int fd;
    static int already_warned = 0;

    fd = open(path, O_WRONLY);
    if (fd >= 0) {
        char buffer[20];
        size_t bytes = snprintf(buffer, sizeof(buffer), "%d\n", value);
        if(bytes >= sizeof(buffer)) return -EINVAL;
        ssize_t amt = write(fd, buffer, bytes);
        close(fd);
        return amt == -1 ? -errno : 0;
    } else {
        if (already_warned == 0) {
            ALOGE("write_int failed to open %s\n", path);
            already_warned = 1;
        }
        return -errno;
    }
}

#if BOARD_HAS_BATTERY_LIGHTS
static int set_light_battery(struct light_device_t* dev,
                             struct light_state_t const* state)
{
    int red, green, blue;
    unsigned int colorRGB;

    if (!dev)
        return -1;

    if (state->flashMode == LIGHT_FLASH_TIMED) {
        ALOGI("Discard FLASH_TIMED info as not supported!");
        return 0;
    }

    colorRGB = state->color;

    ALOGV("set_speaker_light_locked mode %d, colorRGB=%08X\n",
            state->flashMode, colorRGB);

    red = ((colorRGB >> 16) & 0xFF) * rgb_brightness_ratio / 255;
    green = ((colorRGB >> 8) & 0xFF) * rgb_brightness_ratio / 255;
    blue = (colorRGB & 0xFF) * rgb_brightness_ratio / 255;

    write_int(RED_LED_FILE, red);
    write_int(GREEN_LED_FILE, green);
    write_int(BLUE_LED_FILE, blue);

    return 0;
}
#endif

static int set_light_backlight(struct light_device_t* dev,
                               struct light_state_t const* state)
{
    int result = -1;
    unsigned int color = state->color;
    unsigned int brightness = 0, max_brightness = 0;
    unsigned int i = 0;
    FILE *file;

    if (!dev)
        return -1;

    brightness = ((77*((color>>16)&0x00ff)) + (150*((color>>8)&0x00ff)) +
                 (29*(color&0x00ff))) >> 8;
    ALOGV("set_light, get brightness=%d", brightness);

    file = fopen(backlight_max_path, "r");
    if (!file) {
        ALOGE("can not open file %s\n", backlight_max_path);
        return result;
    }
    fread(&max_brightness, 1, 3, file);
    fclose(file);

    max_brightness = atoi((char *) &max_brightness);
    /* any brightness greater than 0, should have at least backlight on */
    if (max_brightness < MAX_BRIGHTNESS)
        brightness = max_brightness *(brightness + MAX_BRIGHTNESS / max_brightness - 1) / MAX_BRIGHTNESS;
    else
        brightness = max_brightness * brightness / MAX_BRIGHTNESS;

    if (brightness > max_brightness) {
        brightness  = max_brightness;
    }
    if (brightness < 1) {/*target brightness should be at least 1, or backlight will off*/
        brightness  = 1;
    }
    ALOGV("set_light, max_brightness=%d, target brightness=%d",
        max_brightness, brightness);

    result = write_int(backlight_path, brightness);

    return result;
}

static int close_lights(struct hw_device_t *dev)
{
    struct light_device_t *device = (struct light_device_t*)dev;
    if (device)
        free(device);
    return 0;
}

/*****************************************************************************/
static int lights_device_open(const struct hw_module_t* module,
                              const char* name, struct hw_device_t** device)
{
    int (*set_light)(struct light_device_t* dev,
            struct light_state_t const* state);

    if (!strcmp(name, LIGHT_ID_BACKLIGHT)) {
        int fdfb0;
        char value[PROPERTY_VALUE_MAX];
        char fbtype[256];
        struct dirent **namelist;
        int n, idx = 0;
        FILE *file;

        set_light = set_light_backlight;
#ifdef BOARD_IS_IMX6
        strcpy(fbtype,"ldb"); /* default */
        fdfb0 = open("/sys/class/graphics/fb0/fsl_disp_dev_property", O_RDONLY);
        if (0 <= fdfb0) {
            char buf[256];
            int len;
            if (0 < (len=read(fdfb0,buf,sizeof(buf)-1))) {
                int i;
                buf[len] = '\0';
                for (i=0; i < len; i++) {
                    if (!isprint(buf[i])) {
                        buf[i] = '\0';
                        strcpy(fbtype,buf);
                        break;
                    }
                }
            }
            else
                buf[0]=0;
            close(fdfb0);
        }

        /* The sysfs entry is either backlight_lcd.x or backlight_lvds.x */
        if (!strcmp(fbtype, "ldb"))
            strcpy(fbtype, "lvds");

        /* Search for the appropriate backlight */
        n = scandir(DEF_BACKLIGHT_PATH, &namelist, NULL, alphasort);
        if (n < 0) {
            ALOGE("couldn't scandir %s", DEF_BACKLIGHT_PATH);
            return -EINVAL;
        }

        /* Use first match (alphabetic order) */
        for (idx = 0; idx < n; idx++) {
            if (strstr(namelist[idx]->d_name, fbtype) != NULL) {
                strcpy(backlight_path, DEF_BACKLIGHT_PATH);
                strcat(backlight_path, namelist[idx]->d_name);
                ALOGI("found backlight under %s", backlight_path);
                break;
            }
        }

        /* Make sure the whole list is freed */
        while (n--)
            free(namelist[n]);
        free(namelist);

        /* Check if a backlight_path has been found */
        if (strstr(backlight_path, DEF_BACKLIGHT_PATH) == NULL) {
            ALOGE("didn't find any matching backlight");
            return -EINVAL;
        }
#else
        property_get("ro.backlight.dev", value, DEF_BACKLIGHT_DEV);
        strcpy(backlight_path, DEF_BACKLIGHT_PATH);
        strcat(backlight_path, value);
#endif

        strcpy(backlight_max_path, backlight_path);
        strcat(backlight_max_path, "/max_brightness");
        strcat(backlight_path, "/brightness");

        file = fopen(backlight_path, "r");
        if (!file) {
            ALOGE("cannot open backlight %s\n", backlight_path);
            return -EINVAL;
        }
        fclose(file);

        ALOGI("max backlight file is %s\n", backlight_max_path);
        ALOGI("backlight brightness file is %s\n", backlight_path);
    }
#if BOARD_HAS_BATTERY_LIGHTS
    else if (0 == strcmp(LIGHT_ID_BATTERY, name))
        set_light = set_light_battery;
#endif

    struct light_device_t *dev = malloc(sizeof(struct light_device_t));

    if(!dev)
        return -ENOMEM;

    memset(dev, 0, sizeof(*dev));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t*) module;
    dev->common.close = close_lights;
    dev->set_light = set_light;

    *device = (struct hw_device_t*)dev;

    return 0;
}
