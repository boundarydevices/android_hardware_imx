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
#include <poll.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/select.h>
#include <stdlib.h>
#include <cutils/log.h>

#include <linux/input.h>

#include "SensorBase.h"

/*****************************************************************************/
#define  SYSFS_ENABLE  	"enable"
#define  SYSFS_POLL   	"poll"
#define  SYSFS_POLL_MIN "min"
#define  SYSFS_POLL_MAX	"max"
int SensorBase::mUser[SENSORS_MAX] 	= {0};
uint32_t SensorBase::mEnabled		= 0;
uint32_t SensorBase::mPendingMask	= 0;

sensors_event_t SensorBase::mPendingEvents[SensorBase::numSensors];
SensorBase::SensorBase(
        const char* dev_name,
        const char* data_name)
    : dev_name(dev_name), data_name(data_name),
      dev_fd(-1), data_fd(-1),
      mInputReader(64)
{
    if (data_name)
        data_fd = openInput(data_name);
}

SensorBase::~SensorBase()
{
    if (data_fd >= 0)
        close(data_fd);

    if (dev_fd >= 0)
        close(dev_fd);
}

int SensorBase::open_device()
{
    if (dev_fd<0 && dev_name) {
        dev_fd = open(dev_name, O_RDONLY);
        LOGE_IF(dev_fd<0, "Couldn't open %s (%s)", dev_name, strerror(errno));
    }
    return 0;
}

int SensorBase::close_device()
{
    if (dev_fd >= 0) {
        close(dev_fd);
        dev_fd = -1;
    }
    return 0;
}

int SensorBase::write_sysfs(char * filename,char * buf,int size)
{
    int fd;
    if(filename == NULL || buf == NULL || size <= 0 )
        return -1;
    fd = open(filename,O_WRONLY);
    if(fd > 0){
        write(fd,buf,size);
        close(fd);
    }
    else
        return -1;
    return 0;
}

int SensorBase::read_sysfs(char * filename,char * buf,int size){
    int fd;
    int count = 0;
    if(filename == NULL || buf == NULL || size <= 0)
        return 0;
    fd = open(filename,O_RDONLY);
    if(fd > 0){
        count = read(fd,buf,size);
        close(fd);
    } else {
        LOGE("read sysfs file error\n");
        return 0;
    }
    return count;
}

int SensorBase::sensorBaseEnable(int32_t handle,int enabled){
    char buf[6];
    int enable = (enabled ? 1 : 0);
    int what = -1;
    /*if the munber of  user > 1, do not disable sensor*/
    switch (handle) {
        case ID_A	: what = Accelerometer ;break;
        case ID_M	: what = MagneticField; break;
        case ID_O	: what = Orientation;   break;
        case ID_GY 	: what = Gryo;   break;
        case ID_L 	: what = Light;   break;
        case ID_P  	: what = Pressure;   break;
        case ID_T  	: what = Temperatury;   break;
        case ID_PX 	: what = Proximity;   break;

    }
    if (what < 0 || what >= numSensors)
        return -EINVAL;

    if(enable)
        mUser[what]++;
    else {
        mUser[what]--;
        if(mUser[what] < 0)
            mUser[what] = 0;
    }
    if((enable && mUser[what] == 1) || (enable ==0  &&  mUser[what] == 0 )) {
        snprintf(buf,sizeof(buf),"%d",enable);
        write_sysfs(sysfs_enable,buf,strlen(buf));
        mEnabled &= ~(1<<what);
        mEnabled |= (uint32_t(enable)<<what);
    }

    LOGD("sensor %d , usr count %d\n",handle,mUser[handle]);
    return 0;
}

int SensorBase::sensorBaseSetDelay(int32_t handle, int64_t ns){
    char buf[6];
    int ms;
    ms = ns/1000/1000;
    if(ms < mMinPollDelay)
        ms = mMinPollDelay ;
    else if(ms > mMaxPollDelay)
        ms = mMaxPollDelay;
    snprintf(buf,sizeof(buf),"%d",ms);
    return write_sysfs(sysfs_poll,buf,strlen(buf));
}

int SensorBase::sensorBaseGetPollMin(){
    char buf[64];
    int size;
    int pollmin;
    size = read_sysfs(sysfs_poll_min,buf,sizeof(buf));
    buf[size] = '\0';
    pollmin = atoi(buf);
    LOGD("%s ,%s",__FUNCTION__,buf);
    return pollmin;
}

int SensorBase::sensorBaseGetPollMax(){
    char buf[64];
    int size;
    int pollmax;
    size = read_sysfs(sysfs_poll_max,buf,sizeof(buf));
    buf[size] = '\0';
    pollmax = atoi(buf);
    LOGD("%s ,%s",__FUNCTION__,buf);
    return pollmax; //default max is 200ms
}

int SensorBase::sensorBaseGetSysfsPath(const char* inputName)
{
    FILE *fd = NULL;
    const char *dirname = "/sys/class/input/";
    char sysfs_name[PATH_MAX], *endptr;
    char *filename = NULL, buf[32];
    DIR *dir;
    struct dirent *de;
    int n, path_len;
    dir = opendir(dirname);
    if(dir == NULL)
        return -1;

    strcpy(sysfs_name, dirname);
    filename = sysfs_name + strlen(sysfs_name);
    while ((de = readdir(dir))) {
        if ((strlen(de->d_name) < 6) ||
            strncmp(de->d_name, "input", 5))
            continue;

        strcpy(filename, de->d_name);
        strcat(filename, "/");
        path_len = strlen(sysfs_name);
        strcat(filename, "name");
        fd = fopen(sysfs_name, "r");
        if (fd) {
            memset(buf, 0, 32);
            n = fread(buf, 1, 32, fd);
            fclose(fd);
            if ((strlen(buf) >= strlen(inputName)) &&
                !strncmp(buf, inputName, strlen(inputName))) {
                 sysfs_name[path_len] = '\0';
                 snprintf(sysfs_enable, sizeof(sysfs_enable), "%s%s",sysfs_name,SYSFS_ENABLE);
                 snprintf(sysfs_poll, sizeof(sysfs_poll), "%s%s",sysfs_name,SYSFS_POLL);
                 snprintf(sysfs_poll_min, sizeof(sysfs_poll_min), "%s%s",sysfs_name, SYSFS_POLL_MIN);
                 snprintf(sysfs_poll_max, sizeof(sysfs_poll_max), "%s%s",sysfs_name, SYSFS_POLL_MAX);
                 mMinPollDelay = sensorBaseGetPollMin();
                 mMaxPollDelay = sensorBaseGetPollMax();
                 LOGD("%s path %s",inputName,sysfs_enable);
                 LOGD("%s path %s",inputName,sysfs_poll);
                 LOGD("%s path %s ,poll min delay %d",inputName,sysfs_poll_min,mMinPollDelay);
                 LOGD("%s path %s ,poll max delay %d",inputName,sysfs_poll_max,mMaxPollDelay);
                 return 0;
            }
        }
    }

    return -1;
}

int SensorBase::getFd() const
{
    if (!data_name) {
        return dev_fd;
    }
    return data_fd;
}

int SensorBase::enable(int32_t handle, int enabled)
{
    sensorBaseEnable(handle,enabled);
    return 0;
}

int SensorBase::setDelay(int32_t handle, int64_t ns)
{
    sensorBaseSetDelay(handle,ns);
    return 0;
}

bool SensorBase::hasPendingEvents() const
{
    return false;
}

void processEvent(int code, int value)
{
}

int64_t SensorBase::getTimestamp()
{
    struct timespec t;
    t.tv_sec = t.tv_nsec = 0;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return int64_t(t.tv_sec)*1000000000LL + t.tv_nsec;
}

int SensorBase::openInput(const char* inputName)
{
    int fd = -1;
    int input_id = -1;
    const char *dirname = "/dev/input";
    const char *inputsysfs = "/sys/class/input";
    char devname[PATH_MAX];
    char *filename;
    DIR *dir;
    struct dirent *de;

    dir = opendir(dirname);
    if(dir == NULL)
        return -1;
    strcpy(devname, dirname);
    filename = devname + strlen(devname);
    *filename++ = '/';
    while((de = readdir(dir))) {
        if(de->d_name[0] == '.' &&
                (de->d_name[1] == '\0' ||
                        (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;
        strcpy(filename, de->d_name);
        fd = open(devname, O_RDONLY);

        if (fd>=0) {
            char name[80];
            if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), &name) < 1) {
                name[0] = '\0';
            }

            if (!strcmp(name, inputName)) {
                strcpy(input_name, filename);
                break;
            } else {
                close(fd);
                fd = -1;
            }
        }
    }
    closedir(dir);
    LOGE_IF(fd<0, "couldn't find '%s' input device", inputName);
    return fd;
}

int SensorBase::readEvents(sensors_event_t* data, int count)
{
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
            processEvent(event->code, event->value);
            mInputReader.next();
        } else if (type == EV_SYN) {
            int64_t time = timevalToNano(event->time);

            for (int j=0 ; count && mPendingMask && j<numSensors ; j++) {

                if (mPendingMask & (1<<j)) {
                    mPendingMask &= ~(1<<j);
                    mPendingEvents[j].timestamp = time;
                    if (mEnabled & (1<<j)) {
                        *data++ = mPendingEvents[j];
                        count--;
                        numEventReceived++;
                    }
                }
            }
            if (!mPendingMask) {
                mInputReader.next();
            }
        } else {
            LOGE("Sensor: unknown event (type=%d, code=%d)",
                    type, event->code);
            mInputReader.next();
        }
    }

    return numEventReceived;
}

