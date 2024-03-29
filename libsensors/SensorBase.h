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

#ifndef ANDROID_SENSOR_BASE_H
#define ANDROID_SENSOR_BASE_H

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/cdefs.h>
#include <sys/types.h>

#include "InputEventReader.h"
#include "sensors.h"

#define SENSORS_MAX 20

/*****************************************************************************/
class SensorBase {
protected:
    const char* dev_name;
    const char* data_name;
    const char* fifo_name;
    char input_name[PATH_MAX];
    int dev_fd;
    int data_fd;
    int fifo_fd;
    int mBatchEnabled;
    int mFlushed;
    int openInput(const char* inputName);
    static int64_t getTimestamp();

    static int64_t timevalToNano(timeval const& t) {
        return t.tv_sec * 1000000000LL + t.tv_usec * 1000;
    }

    int open_device();
    int close_device();
    int open_fifo_device();
    int close_fifo_device();

public:
    SensorBase(const char* dev_name, const char* data_name);
    SensorBase(const char* dev_name, const char* data_name, const char* fifo_name);
    virtual ~SensorBase();
    virtual bool hasPendingEvents() const;
    virtual int getFd() const;
    virtual int setDelay(int32_t handle, int64_t ns);
    virtual int setEnable(int32_t handle, int enabled);
    virtual int getEnable(int32_t handle);
    virtual int readEvents(sensors_event_t* data, int count);
    virtual void processEvent(int code, int value) = 0;
    virtual int batch(int handle, int flags, int64_t period_ns, int64_t timeout);
    virtual int flush(int handle);
};

/*****************************************************************************/

#endif // ANDROID_SENSOR_BASE_H
