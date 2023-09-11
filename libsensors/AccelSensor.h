/*
 * Copyright (C) 2011-2014 Freescale Semiconductor, Inc.
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

#ifndef ANDROID_FSL_ACCEL_SENSOR_H
#define ANDROID_FSL_ACCEL_SENSOR_H

#include <errno.h>
#include <stdint.h>
#include <sys/cdefs.h>
#include <sys/types.h>

#include "InputEventReader.h"
#include "SensorBase.h"
#include "sensors.h"

#define MMA8X5X_FIFO_SIZE 32

/*****************************************************************************/
class AccelSensor : public SensorBase {
public:
    AccelSensor();
    virtual ~AccelSensor();
    virtual int setDelay(int32_t handle, int64_t ns);
    virtual int setEnable(int32_t handle, int enabled);
    virtual int getEnable(int32_t handle);
    virtual int readEvents(sensors_event_t* data, int count);
    virtual bool hasPendingEvents();
    void processEvent(int code, int value);
    int batch(int handle, int flags, int64_t period_ns, int64_t timeout);
    int flush(int handle);

private:
    int sensor_get_class_path(char* class_path);
    int is_sensor_enabled();
    int enable_sensor();
    int disable_sensor();
    int set_delay(int64_t ns);
    int update_delay();
    int readDisable();
    int writeEnable(int isEnable);
    int writeDelay(int64_t ns);
    int fifo(int64_t period_ns, int64_t timeout_ns, int wakeup);
    int read_fifo();
    int mUser;
    int mEnabled;
    int mPendingMask;
    char mClassPath[PATH_MAX];
    InputEventCircularReader mInputReader;
    sensors_event_t mPendingEvent;
    int mFifoCount;
    sensors_event_t mFifoPendingEvent[MMA8X5X_FIFO_SIZE];
    int64_t mDelay;
};

/*****************************************************************************/

#endif // ANDROID_FSL_ACCEL_SENSOR_H
