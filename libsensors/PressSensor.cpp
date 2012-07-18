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

#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <poll.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/select.h>
#include <dlfcn.h>
#include <cutils/log.h>

#include "PressSensor.h"

PressSensor::PressSensor()
:SensorBase(NULL, "mpl3115")
{
    const char *magSensorName = "mpl3115";
    sensorBaseGetSysfsPath(magSensorName);
    memset(&mPendingEvents[Pressure], 0, sizeof(sensors_event_t));
    memset(&mPendingEvents[Temperatury], 0, sizeof(sensors_event_t));

    mPendingEvents[Pressure].version = sizeof(sensors_event_t);
    mPendingEvents[Pressure].sensor = ID_P;
    mPendingEvents[Pressure].type = SENSOR_TYPE_PRESSURE;

    mPendingEvents[Temperatury].version = sizeof(sensors_event_t);
    mPendingEvents[Temperatury].sensor = ID_T;
    mPendingEvents[Temperatury].type = SENSOR_TYPE_TEMPERATURE;

}

PressSensor::~PressSensor()
{
}

void PressSensor::processEvent(int code, int value)
{
    switch (code) {
        case EVENT_TYPE_PRESSURE:
            mPendingMask |= 1 << Pressure;
            mPendingEvents[Pressure].pressure= value * CONVERT_PRESSURE;
            break;
		case EVENT_TYPE_TEMPERATURE:
            mPendingMask |= 1<<Temperatury;
            mPendingEvents[Temperatury].temperature= value * CONVERT_TEMPERATURE;
            break;
    }
}
