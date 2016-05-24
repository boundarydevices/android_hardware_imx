/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright 2009-2015 Freescale Semiconductor, Inc.
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

#define MAX_BRIGHTNESS 255
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

static char max_path[256], path[256];
// ****************************************************************************
// module
// ****************************************************************************
static int set_light_backlight(struct light_device_t* dev,
                               struct light_state_t const* state)
{
    int result = -1;
    unsigned int color = state->color;
    unsigned int brightness = 0, max_brightness = 0;
    unsigned int i = 0;
    FILE *file;

    brightness = ((77*((color>>16)&0x00ff)) + (150*((color>>8)&0x00ff)) +
                 (29*(color&0x00ff))) >> 8;
    ALOGV("set_light, get brightness=%d", brightness);

    file = fopen(max_path, "r");
    if (!file) {
        ALOGE("can not open file %s\n", max_path);
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
    
    ALOGV("set_light, max_brightness=%d, target brightness=%d",
        max_brightness, brightness);

    file = fopen(path, "w");
    if (!file) {
        ALOGE("can not open file %s\n", path);
        return result;
    }
    fprintf(file, "%d", brightness);
    fclose(file);

    result = 0;
    return result;
}

static int light_close_backlight(struct hw_device_t *dev)
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
    int status = -EINVAL;
    ALOGV("lights_device_open\n");
    if (!strcmp(name, LIGHT_ID_BACKLIGHT)) {
        struct light_device_t *dev;
	int fdfb0;
	char fbtype[256];
	DIR *d;

        dev = malloc(sizeof(*dev));

        /* initialize our state here */
        memset(dev, 0, sizeof(*dev));

        /* initialize the procs */
        dev->common.tag = HARDWARE_DEVICE_TAG;
        dev->common.version = 0;
        dev->common.module = (struct hw_module_t*) module;
        dev->common.close = light_close_backlight;

        dev->set_light = set_light_backlight;

        *device = &dev->common;

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
	    strcpy(fbtype,"lvds");

	/* Search for the appropriate backlight */
	d = opendir(DEF_BACKLIGHT_PATH);
	if (! d) {
		ALOGE("couldn't open %s", DEF_BACKLIGHT_PATH);
		return status;
	}
	while (1) {
		struct dirent *entry;

		entry = readdir (d);
		if (!entry)
			break;
		if (strstr(entry->d_name, fbtype) != NULL) {
			strcpy(path, DEF_BACKLIGHT_PATH);
			strcat(path, entry->d_name);
			ALOGI("found backlight under %s", path);
			break;
		}
	}

	/* Check if a path has been found */
	if (strstr(path, DEF_BACKLIGHT_PATH) == NULL) {
		ALOGE("didn't find any matching backlight");
		return status;
	}

	strcpy(max_path, path);
	strcat(max_path, "/max_brightness");
	strcat(path, "/brightness");

        ALOGI("max backlight file is %s\n", max_path);
        ALOGI("backlight brightness file is %s\n", path);

        status = 0;
    }

    /* todo other lights device init */
    return status;
}
