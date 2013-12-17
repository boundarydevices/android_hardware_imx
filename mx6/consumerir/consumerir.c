/*
 * Copyright (C) 2009-2013 Freescale Semiconductor, Inc. All Rights Reserved.
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

#define LOG_TAG "ConsumerIrHal"

#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <cutils/log.h>
#include <hardware/hardware.h>
#include <hardware/consumerir.h>
#include <linux/ioctl.h>
#include <linux/ir.h>


#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define DEF_CONSUMERIR_DEV "/dev/ir"

static int consumerir_open(const hw_module_t* module, const char* name,
        hw_device_t** device);

static struct hw_module_methods_t consumerir_module_methods = {
    .open = consumerir_open,
};

consumerir_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag                = HARDWARE_MODULE_TAG,
        .module_api_version = CONSUMERIR_MODULE_API_VERSION_1_0,
        .hal_api_version    = HARDWARE_HAL_API_VERSION,
        .id                 = CONSUMERIR_HARDWARE_MODULE_ID,
        .name               = "Freescale i.MX IR HAL",
        .author             = "Freescale Semiconductor, Inc.",
        .methods            = &consumerir_module_methods,
    },
};

static int consumerir_transmit(struct consumerir_device *dev,
   int carrier_freq, const int pattern[], int pattern_len)
{
    int fp;
    int result = -1;
    struct ir_data_pattern ir_data;

    memset(&ir_data, 0x00, sizeof(ir_data));
    ir_data.len = pattern_len;
	ir_data.pattern = (int *)pattern;
    fp = open(DEF_CONSUMERIR_DEV,O_RDWR);
    if (-1 == fp) {
         ALOGE("can not open file %s\n", DEF_CONSUMERIR_DEV);
        return -1;
    }
    result = ioctl(fp, IR_CFG_CARRIER,(void *)carrier_freq);
    if(result) {
		ALOGE("can not set IR carrier frequence.\n");
		close(fp);
        return -1;
    }
    result = ioctl(fp,IR_DATA_TRANSMIT,&ir_data);
	if (result) {
		ALOGE("IR data transmit failed.\n");
	} else {
	    ALOGI("IR data transmit succeed.\n");
	}

    close(fp);
    return 0;
}

static int consumerir_get_num_carrier_freqs(struct consumerir_device *dev)
{
    int result = -1;
    int fp;
    int num = 0;
    fp = open(DEF_CONSUMERIR_DEV,O_RDWR);
    if (-1 == fp) {
         ALOGE("can not open file %s\n", DEF_CONSUMERIR_DEV);
        return -1;
    }
    result = ioctl(fp,IR_GET_CARRIER_FREQS_NUM,&num);
	if (result) {
		num = 0;
		ALOGE("IR get num of carrier frequence failed.\n");
	}
    close(fp);
    return num;
}

static int consumerir_get_carrier_freqs(struct consumerir_device *dev,
    size_t len, consumerir_freq_range_t *ranges)
{
    int result = -1;
    int fp;
	size_t freq_num = 0;
	size_t i = 0;
	size_t to_copy;
	struct ir_carrier_freq ir_freq_range;

	if ((!dev) || (!ranges)) {
        return -1;
	}

	memset(&ir_freq_range,0x00,sizeof(ir_freq_range));

    fp = open(DEF_CONSUMERIR_DEV,O_RDWR);
    if (-1 == fp) {
         ALOGE("can not open file %s\n", DEF_CONSUMERIR_DEV);
        return -1;
    }

	result = ioctl(fp, IR_GET_CARRIER_FREQS_NUM, &freq_num);
	if (result) {
		freq_num = 0;
		ALOGE("IR get num of carrier frequence failed.\n");
		return -1;
	}

	to_copy = freq_num < len ? freq_num : len;
	for (i = 0; i < to_copy; i++)
	{
	    ir_freq_range.id = i;
        result = ioctl(fp,IR_GET_CARRIER_FREQS,&ir_freq_range);
        if (result) {
	        ALOGE("IR get carrier frequence failed.\n");
        } else {
            ranges[i].min = ir_freq_range.min;
            ranges[i].max = ir_freq_range.max;
	        ALOGI("min=%d,max:%d\n",ir_freq_range.min,ir_freq_range.max);
        }
	}
    close(fp);
    return to_copy;
}

static int consumerir_close(hw_device_t *dev)
{
    free(dev);
    return 0;
}

/*
 * Generic device handling
 */
static int consumerir_open(const hw_module_t* module, const char* name,
        hw_device_t** device)
{
    if (strcmp(name, CONSUMERIR_TRANSMITTER) != 0) {
        return -EINVAL;
    }
    if (device == NULL) {
        ALOGE("NULL device on open");
        return -EINVAL;
    }

    consumerir_device_t *dev = malloc(sizeof(consumerir_device_t));
    memset(dev, 0, sizeof(consumerir_device_t));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t*) module;
    dev->common.close = consumerir_close;

    dev->transmit = consumerir_transmit;
    dev->get_num_carrier_freqs = consumerir_get_num_carrier_freqs;
    dev->get_carrier_freqs = consumerir_get_carrier_freqs;

    *device = (hw_device_t*) dev;
    return 0;
}
