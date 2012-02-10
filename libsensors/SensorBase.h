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

#ifndef ANDROID_SENSOR_BASE_H
#define ANDROID_SENSOR_BASE_H

#include <stdint.h>
#include <errno.h>
#include <sys/cdefs.h>
#include <sys/types.h>
#include "InputEventReader.h"
#include "sensors.h"

#define SENSORS_MAX  20

/*****************************************************************************/
class SensorBase {
protected:
    const char* dev_name;
    const char* data_name;
    char   input_name[PATH_MAX];
    char   sysfs_enable[PATH_MAX];
    char   sysfs_poll[PATH_MAX];
    char   sysfs_poll_min[PATH_MAX];
    char   sysfs_poll_max[PATH_MAX];
    int    dev_fd;
    int    data_fd;
    int    mMinPollDelay;
    int    mMaxPollDelay;
    int    openInput(const char* inputName);
    static int64_t getTimestamp();


    static int64_t timevalToNano(timeval const& t) {
        return t.tv_sec*1000000000LL + t.tv_usec*1000;
    }

    int open_device();
    int close_device();
    int write_sysfs(char * filename,char * buf,int size);
    int read_sysfs(char * filename,char * buf,int size);
    int sensorBaseEnable(int32_t handle,int enabled);
    int sensorBaseSetDelay(int32_t handle, int64_t ns);
    int sensorBaseGetPollMin();
    int sensorBaseGetPollMax();
    int sensorBaseGetSysfsPath(const char* inputName);
    InputEventCircularReader mInputReader;

public:
    static int mUser[SENSORS_MAX];
    static const int    Accelerometer   = 0;
    static const int	MagneticField   = 1;
    static const int	Orientation = 2;
    static const int	Gryo =	3;
    static const int	Light  = 4;
    static const int	Pressure = 5;
    static const int	Temperatury = 6;
    static const int	Proximity = 7;
    static const int	numSensors = 8 ;
    static uint32_t mEnabled;
    static uint32_t mPendingMask;
    static sensors_event_t mPendingEvents[numSensors];
    SensorBase(
               const char* dev_name,
               const char* data_name);

    virtual ~SensorBase();

    virtual int readEvents(sensors_event_t* data, int count);
    virtual bool hasPendingEvents() const;
    virtual int getFd() const;
    virtual int setDelay(int32_t handle, int64_t ns);
    virtual int enable(int32_t handle, int enabled) ;
    virtual void processEvent(int code, int value) = 0;
};

/*****************************************************************************/

#endif  // ANDROID_SENSOR_BASE_H
