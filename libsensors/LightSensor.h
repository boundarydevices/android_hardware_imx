/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2011-2012 Freescale Semiconductor, Inc.
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

#ifndef ANDROID_LIGHT_SENSOR_H
#define ANDROID_LIGHT_SENSOR_H

#include <errno.h>
#include <stdint.h>
#include <sys/cdefs.h>
#include <sys/types.h>

#include "InputEventReader.h"
#include "SensorBase.h"
#include "sensors.h"

#define ISL29023_ALS_CONT_MODE 5

/*****************************************************************************/

struct input_event;

class LightSensor : public SensorBase {
    int mEnabled;
    InputEventCircularReader mInputReader;
    sensors_event_t mPendingEvent;
    bool mHasPendingEvent;
    char ls_sysfs_path[PATH_MAX];
    int ls_sysfs_path_len;
    float mPreviousLight;

public:
    LightSensor();
    virtual ~LightSensor();
    virtual int readEvents(sensors_event_t* data, int count);
    virtual bool hasPendingEvents() const;
    virtual int setDelay(int32_t handle, int64_t ns);
    virtual int enable(int32_t handle, int enabled);
    virtual void processEvent(int code, int value);

private:
    int mThresholdLux;
    int setIntLux();
};

/*****************************************************************************/

#endif // ANDROID_LIGHT_SENSOR_H
