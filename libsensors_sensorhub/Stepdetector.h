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

#ifndef ANDROID_STEPDETECTOR__SENSOR_H
#define ANDROID_STEPDETECTOR__SENSOR_H

#include "sensors.h"
#include "SensorBase.h"
#include "InputEventReader.h"

/*****************************************************************************/

class Stepdetector : public SensorBase {
public:
    Stepdetector();
    virtual ~Stepdetector();
    virtual int setDelay(int32_t handle, int64_t ns);
    virtual int setEnable(int32_t handle, int enabled);
    virtual int getEnable(int32_t handle);
    virtual int readEvents(sensors_event_t* data, int count);
    void processEvent(int code, int value);

private:
    int sensor_get_class_path(char *class_path);
    int is_sensor_enabled();
    int enable_sensor();
    int disable_sensor();
    int readDisable();
    int writeEnable(int isEnable);
    int mEnabled;
    int mPendingMask;
    char mClassPath[PATH_MAX];
    InputEventCircularReader mInputReader;
    sensors_event_t mPendingEvent;
};

/*****************************************************************************/

#endif  // ANDROID_FSL_ACCEL_SENSOR_H
