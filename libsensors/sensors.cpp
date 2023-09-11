/*
 * Copyright (C) 2011-2015 Freescale Semiconductor, Inc.
 * Copyright (C) 2008 The Android Open Source Project
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

#define LOG_TAG "Sensors"

#include "sensors.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <hardware/sensors.h>
#include <linux/input.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <utils/Atomic.h>
#include <utils/Log.h>

#include "AccelSensor.h"
#include "LightSensor.h"
#include "MagSensor.h"
#include "PressSensor.h"

/*****************************************************************************/

#define DELAY_OUT_TIME 0x7FFFFFFF

#define LIGHT_SENSOR_POLLTIME 2000000000

#define SENSORS_ACCELERATION (1 << ID_A)
#define SENSORS_MAGNETIC_FIELD (1 << ID_M)
#define SENSORS_ORIENTATION (1 << ID_O)
#define SENSORS_GYROSCOPE (1 << ID_GY)
#define SENSORS_LIGHT (1 << ID_L)
#define SENSORS_PRESS (1 << ID_P)
#define SENSORS_TEMPERATURE (1 << ID_T)
#define SENSORS_PROXIMITY (1 << ID_PX)

#define SENSORS_ACCELERATION_HANDLE ID_A
#define SENSORS_MAGNETIC_FIELD_HANDLE ID_M
#define SENSORS_ORIENTATION_HANDLE ID_O
#define SENSORS_GYROSCOPE_HANDLE ID_GY
#define SENSORS_LIGHT_HANDLE ID_L
#define SENSORS_PRESSURE_HANDLE ID_P
#define SENSORS_TEMPERATURE_HANDLE ID_T
#define SENSORS_PROXIMITY_HANDLE ID_PX

/*****************************************************************************/

/* The SENSORS Module */
static const struct sensor_t sSensorList[] = {
        {.name = "Freescale 3-axis Accelerometer",
         .vendor = "Freescale Semiconductor Inc.",
         .version = 1,
         .handle = SENSORS_ACCELERATION_HANDLE,
         .type = SENSOR_TYPE_ACCELEROMETER,
         .maxRange = RANGE_A,
         .resolution = CONVERT_A,
         .power = 0.30f,
         .minDelay = 2500,
         .fifoReservedEventCount = 0,
         .fifoMaxEventCount = 32,
         .stringType = SENSOR_STRING_TYPE_ACCELEROMETER,
         .requiredPermission = 0,
         .maxDelay = 640000,
         .flags = SENSOR_FLAG_CONTINUOUS_MODE,
         .reserved = {}},
        {.name = "Freescale 3-axis Magnetic field sensor",
         .vendor = "Freescale Semiconductor Inc.",
         .version = 1,
         .handle = SENSORS_MAGNETIC_FIELD_HANDLE,
         .type = SENSOR_TYPE_MAGNETIC_FIELD,
         .maxRange = 1500.0f,
         .resolution = CONVERT_M,
         .power = 0.50f,
         .minDelay = 2500,
         .fifoReservedEventCount = 0,
         .fifoMaxEventCount = 0,
         .stringType = SENSOR_STRING_TYPE_MAGNETIC_FIELD,
         .requiredPermission = 0,
         .maxDelay = 640000,
         .flags = SENSOR_FLAG_CONTINUOUS_MODE,
         .reserved = {}},
        {.name = "Freescale Orientation sensor",
         .vendor = "Freescale Semiconductor Inc.",
         .version = 1,
         .handle = SENSORS_ORIENTATION_HANDLE,
         .type = SENSOR_TYPE_ORIENTATION,
         .maxRange = 360.0f,
         .resolution = CONVERT_O,
         .power = 0.50f,
         .minDelay = 2500,
         .fifoReservedEventCount = 0,
         .fifoMaxEventCount = 0,
         .stringType = SENSOR_STRING_TYPE_ORIENTATION,
         .requiredPermission = 0,
         .maxDelay = 640000,
         .flags = SENSOR_FLAG_CONTINUOUS_MODE,
         .reserved = {}},
        {.name = "ISL29023 Light sensor",
         .vendor = "Intersil",
         .version = 1,
         .handle = SENSORS_LIGHT_HANDLE,
         .type = SENSOR_TYPE_LIGHT,
         .maxRange = 16000.0f,
         .resolution = 1.0f,
         .power = 0.35f,
         .minDelay = 0,
         .fifoReservedEventCount = 0,
         .fifoMaxEventCount = 0,
         .stringType = SENSOR_STRING_TYPE_LIGHT,
         .requiredPermission = 0,
         .maxDelay = 0,
         .flags = SENSOR_FLAG_ON_CHANGE_MODE,
         .reserved = {}},
};

static int open_sensors(const struct hw_module_t *module, const char *id,
                        struct hw_device_t **device);

static int sensors__get_sensors_list(struct sensors_module_t *module,
                                     struct sensor_t const **list) {
    *list = sSensorList;
    return ARRAY_SIZE(sSensorList);
}

static struct hw_module_methods_t sensors_module_methods = {open : open_sensors};

struct sensors_module_t HAL_MODULE_INFO_SYM = {
    common : {
        tag : HARDWARE_MODULE_TAG,
        version_major : 1,
        version_minor : 1,
        id : SENSORS_HARDWARE_MODULE_ID,
        name : "Freescale Sensor module",
        author : "Freescale Semiconductor Inc.",
        methods : &sensors_module_methods,
    },
    get_sensors_list : sensors__get_sensors_list,
};
struct BatchParameter {
    int flags;
    int64_t period_ns;
    int64_t timeout;
};
struct sensors_poll_context_t {
    struct sensors_poll_device_1 device; // must be first

    sensors_poll_context_t();
    ~sensors_poll_context_t();
    int fillPollFd();
    int activate(int handle, int enabled);
    int setDelay(int handle, int64_t ns);
    int pollEvents(sensors_event_t *data, int count);
    int batch(int handle, int flags, int64_t period_ns, int64_t timeout);
    int flush(int handle);
    struct BatchParameter mBatchParameter[32];
    int magRunTimes;

private:
    enum {
        accel = 0,
        mag = 1,
        light = 2,
        numSensorDrivers,
        numFds,
    };
    static const size_t wake = numFds - 1;
    static const char WAKE_MESSAGE = 'W';
    struct pollfd mPollFds[numFds];
    int mWritePipeFd;
    SensorBase *mSensors[numSensorDrivers];

    int handleToDriver(int handle) const {
        switch (handle) {
            case ID_A:
                return accel;
            case ID_M:
            case ID_O:
                return mag;
            case ID_L:
                return light;
        }
        return -EINVAL;
    }
};

/*****************************************************************************/
int sensors_poll_context_t::fillPollFd() {
    int i = 0;
    for (i = 0; i < numSensorDrivers; i++) {
        if (mSensors[i] != NULL) mPollFds[i].fd = mSensors[i]->getFd();
        mPollFds[i].events = POLLIN;
        mPollFds[i].revents = 0;
    }
    return 0;
}
sensors_poll_context_t::sensors_poll_context_t() {
    mSensors[accel] = new AccelSensor();
    mSensors[mag] = new MagSensor();
    mSensors[light] = new LightSensor();
    fillPollFd();
    magRunTimes = 0;
    int wakeFds[2];
    int result = pipe(wakeFds);
    ALOGE_IF(result < 0, "error creating wake pipe (%s)", strerror(errno));
    fcntl(wakeFds[0], F_SETFL, O_NONBLOCK);
    fcntl(wakeFds[1], F_SETFL, O_NONBLOCK);
    mWritePipeFd = wakeFds[1];

    mPollFds[wake].fd = wakeFds[0];
    mPollFds[wake].events = POLLIN;
    mPollFds[wake].revents = 0;
}

sensors_poll_context_t::~sensors_poll_context_t() {
    for (int i = 0; i < numSensorDrivers; i++) {
        delete mSensors[i];
    }
    close(mPollFds[wake].fd);
    close(mWritePipeFd);
}

int sensors_poll_context_t::activate(int handle, int enabled) {
    int index = handleToDriver(handle);
    if (index < 0) return index;
    int err = 0;
    if (handle == ID_A) {
        if (enabled && magRunTimes > 0)
            err = mSensors[index]->batch(handle, 0, mBatchParameter[handle].period_ns,
                                         0); // forece accel to be continuse mode
        err |= mSensors[index]->setEnable(handle, enabled);
    } else if (handle == ID_O || handle == ID_M) {
        if (enabled) {
            magRunTimes++;
        } else {
            magRunTimes--;
            if (magRunTimes < 0) magRunTimes = 0;
        }
        if (magRunTimes > 0)
            err |= mSensors[handleToDriver(ID_A)]->batch(ID_A, 0, mBatchParameter[handle].period_ns,
                                                         0); // forece accel to be continuse mode
        else
            err |= mSensors[handleToDriver(ID_A)]
                           ->batch(ID_A, 0, mBatchParameter[ID_A].period_ns,
                                   mBatchParameter[ID_A]
                                           .timeout); // forece accel to be continuse mode
        /*change acc user count*/
        err = mSensors[handleToDriver(ID_A)]->setEnable(handle, enabled);
        err |= mSensors[index]->setEnable(handle, enabled);
    } else
        err = mSensors[index]->setEnable(handle, enabled);
    if (!err) {
        const char wakeMessage(WAKE_MESSAGE);
        int result = write(mWritePipeFd, &wakeMessage, 1);
        ALOGE_IF(result < 0, "error sending wake message (%s)", strerror(errno));
    }
    return err;
}

int sensors_poll_context_t::setDelay(int handle, int64_t ns) {
    int index = handleToDriver(handle);
    if (index < 0) return index;
    if (handle == ID_O || handle == ID_M) {
        mSensors[accel]->setDelay(handle, ns); // if handle == orientaion or magnetic ,please enable
                                               // ACCELERATE Sensor
    }
    return mSensors[index]->setDelay(handle, ns);
}

int sensors_poll_context_t::pollEvents(sensors_event_t *data, int count) {
    int nbEvents = 0;
    int n = 0;
    do {
        // see if we have some leftover from the last poll()
        for (int i = 0; count && i < numSensorDrivers; i++) {
            SensorBase *const sensor(mSensors[i]);

            if ((mPollFds[i].revents & POLLIN) || (sensor->hasPendingEvents())) {
                int nb = sensor->readEvents(data, count);
                if (nb < count) {
                    // no more data for this sensor
                    mPollFds[i].revents = 0;
                }
                count -= nb;
                nbEvents += nb;
                data += nb;
            }
        }

        if (count) {
            // we still have some room, so try to see if we can get
            // some events immediately or just wait if we don't have
            // anything to return
            // n = poll(mPollFds, numFds, nbEvents ? 0 : -1);
            do {
                fillPollFd(); /*reset poll fd , if sensor change between batch mode and continuous
                                 mode*/
                n = poll(mPollFds, numFds, nbEvents ? 0 : -1);
            } while (n < 0 && errno == EINTR);
            if (n < 0) {
                ALOGE("poll() failed (%s)", strerror(errno));
                return -errno;
            }
            if (mPollFds[wake].revents & POLLIN) {
                char msg;
                int result = read(mPollFds[wake].fd, &msg, 1);
                ALOGE_IF(result < 0, "error reading from wake pipe (%s)", strerror(errno));
                ALOGE_IF(msg != WAKE_MESSAGE, "unknown message on wake queue (0x%02x)", int(msg));
                mPollFds[wake].revents = 0;
            }
        }
        // if we have events and space, go read them
    } while (n && count);

    return nbEvents;
}
int sensors_poll_context_t::batch(int handle, int flags, int64_t period_ns, int64_t timeout) {
    int ret;
    int index = handleToDriver(handle);
    if (index < 0) return index;
    if (flags & SENSORS_BATCH_DRY_RUN) {
        ret = mSensors[index]->batch(handle, flags, period_ns, timeout);
    } else {
        if (handle == ID_A && magRunTimes > 0)
            ret = mSensors[index]->batch(handle, flags, period_ns, 0);
        else
            ret = mSensors[index]->batch(handle, flags, period_ns, timeout);

        /*just recored the batch parameter for recovery acc batch when mag disable */
        mBatchParameter[handle].flags = flags;
        mBatchParameter[handle].period_ns = period_ns;
        mBatchParameter[handle].timeout = timeout;
        /*wakeup poll , sometime poll may be blocked
         * before sensor was change from batch mode to continuous mode
         */
        const char wakeMessage(WAKE_MESSAGE);
        int result = write(mWritePipeFd, &wakeMessage, 1);
        ALOGE_IF(result < 0, "error batch sending wake message (%s)", strerror(errno));
    }
    return ret;
}
int sensors_poll_context_t::flush(int handle) {
    int index = handleToDriver(handle);
    if (index < 0) return index;
    return mSensors[index]->flush(handle);
}

/*****************************************************************************/

static int poll__close(struct hw_device_t *dev) {
    sensors_poll_context_t *ctx = (sensors_poll_context_t *)dev;
    if (ctx) {
        delete ctx;
    }
    return 0;
}

static int poll__activate(struct sensors_poll_device_t *dev, int handle, int enabled) {
    sensors_poll_context_t *ctx = (sensors_poll_context_t *)dev;
    return ctx->activate(handle, enabled);
}

static int poll__setDelay(struct sensors_poll_device_t *dev, int handle, int64_t ns) {
    sensors_poll_context_t *ctx = (sensors_poll_context_t *)dev;
    return ctx->setDelay(handle, ns);
}

static int poll__poll(struct sensors_poll_device_t *dev, sensors_event_t *data, int count) {
    sensors_poll_context_t *ctx = (sensors_poll_context_t *)dev;
    return ctx->pollEvents(data, count);
}
static int poll__batch(struct sensors_poll_device_1 *dev, int handle, int flags, int64_t period_ns,
                       int64_t timeout) {
    sensors_poll_context_t *ctx = (sensors_poll_context_t *)dev;
    return ctx->batch(handle, flags, period_ns, timeout);
}

static int poll__flush(struct sensors_poll_device_1 *dev, int handle) {
    sensors_poll_context_t *ctx = (sensors_poll_context_t *)dev;
    return ctx->flush(handle);
}
/*****************************************************************************/

/** Open a new instance of a sensor device using name */
static int open_sensors(const struct hw_module_t *module, const char *id,
                        struct hw_device_t **device) {
    int status = -EINVAL;
    sensors_poll_context_t *dev = new sensors_poll_context_t();

    memset(&dev->device, 0, sizeof(sensors_poll_device_1));

    dev->device.common.tag = HARDWARE_DEVICE_TAG;
    dev->device.common.version = SENSORS_DEVICE_API_VERSION_1_4;
    dev->device.common.module = const_cast<hw_module_t *>(module);
    dev->device.common.close = poll__close;
    dev->device.activate = poll__activate;
    dev->device.setDelay = poll__setDelay;
    dev->device.poll = poll__poll;
    dev->device.batch = poll__batch;
    dev->device.flush = poll__flush;
    *device = &dev->device.common;
    status = 0;

    return status;
}
