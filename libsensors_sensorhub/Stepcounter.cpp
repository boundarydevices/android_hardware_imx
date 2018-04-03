/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright 2018 NXP
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

#include <cutils/log.h>
#include "Stepcounter.h"

#define STEPC_DATA_NAME    "step_counter"
#define STEPC_SYSFS_PATH   "/sys/class/misc/step_counter"
#define STEPC_SYSFS_DELAY  "poll_delay"
#define STEPC_SYSFS_ENABLE "enable"

Stepcounter::Stepcounter()
: SensorBase(NULL, STEPC_DATA_NAME),
  mPendingMask(0),
  mInputReader(4) {
    ALOGD("step counter init");
    memset(&mPendingEvent, 0, sizeof(sensors_event_t));
    memset(mClassPath, '\0', sizeof(mClassPath));

    mEnabled = 0;
    mDelay = 0;
    mPendingEvent.version = sizeof(sensors_event_t);
    mPendingEvent.sensor  = ID_SC;
    mPendingEvent.type    = SENSOR_TYPE_STEP_COUNTER;
    mPendingEvent.magnetic.status = SENSOR_STATUS_ACCURACY_MEDIUM;
    mPendingEvent.version = sizeof(sensors_event_t);

    strcpy(mClassPath, STEPC_SYSFS_PATH);
}

Stepcounter::~Stepcounter() {
}

int Stepcounter::setEnable(int32_t handle, int en) {
    int err = 0;
    if(en) {
        mEnabled = 1;
        err = enable_sensor();
    }
    else {
        mEnabled = 0;
        err = disable_sensor();
    }

    if (!err) {
        update_delay();
    }
    return err;
}

int Stepcounter::setDelay(int32_t handle, int64_t ns)
{
    if (ns < 0)
        return -EINVAL;

    mDelay = ns;
    return update_delay();
}

int Stepcounter::update_delay() {
    if (mEnabled) {
        return set_delay(mDelay);
    } else
        return 0;
}

void Stepcounter::processEvent(int code, int value) {
}

int Stepcounter::readEvents(sensors_event_t* data, int count)
{
    int i;
    if (count < 1)
        return -EINVAL;

    ssize_t n = mInputReader.fill(data_fd);
    if (n < 0)
        return n;

    int numEventReceived = 0;
    input_event const* event;

    while (count && mInputReader.readEvent(&event)) {
        int type = event->type;
        if (type == EV_ABS) {
            mPendingEvent.u64.step_counter = event->value;
            mPendingEvent.timestamp = timevalToNano(event->time);
            *data++ = mPendingEvent;
            numEventReceived++;
            mInputReader.next();
        } else {
            mInputReader.next();
        }
    }

    return numEventReceived;
}

int Stepcounter::writeEnable(int isEnable) {
    char attr[PATH_MAX] = {'\0'};
    if(mClassPath[0] == '\0')
        return -1;

    strcpy(attr, mClassPath);
    strcat(attr,"/");
    strcat(attr,STEPC_SYSFS_ENABLE);

    int fd = open(attr, O_RDWR);
    if (0 > fd) {
        ALOGE("Could not open (write-only) SysFs attribute \"%s\" (%s).", attr, strerror(errno));
        return -errno;
    }

    char buf[2];

    if (isEnable) {
        buf[0] = '1';
    } else {
        buf[0] = '0';
    }
    buf[1] = '\0';

    int err = 0;
    err = write(fd, buf, sizeof(buf));

    if (0 > err) {
        err = -errno;
        ALOGE("Could not write SysFs attribute \"%s\" (%s).", attr, strerror(errno));
    } else {
        err = 0;
    }

    close(fd);

    return err;
}

int Stepcounter::writeDelay(int64_t ns) {
    char attr[PATH_MAX] = {'\0'};
    if(mClassPath[0] == '\0')
        return -1;

    strcpy(attr, mClassPath);
    strcat(attr,"/");
    strcat(attr,STEPC_SYSFS_DELAY);

    int fd = open(attr, O_RDWR);
    if (0 > fd) {
        ALOGE("Could not open (write-only) SysFs attribute \"%s\" (%s).", attr, strerror(errno));
        return -errno;
    }
    if (ns > 10240000000LL) {
        ns = 10240000000LL; /* maximum delay in nano second. */
    }
    if (ns < 312500LL) {
        ns = 312500LL; /* minimum delay in nano second. */
    }

    char buf[80];
    sprintf(buf, "%lld", ns/1000/1000);
    write(fd, buf, strlen(buf)+1);
    close(fd);
    return 0;

}

int Stepcounter::enable_sensor() {
    return writeEnable(1);
}

int Stepcounter::disable_sensor() {
    return writeEnable(0);
}

int Stepcounter::set_delay(int64_t ns) {
    return writeDelay(ns);
}

int Stepcounter::getEnable(int32_t handle) {
    return mEnabled;
}

/*****************************************************************************/

