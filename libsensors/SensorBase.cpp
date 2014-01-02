/*
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
SensorBase::SensorBase(
        const char* dev_name,
        const char* data_name)
    : dev_name(dev_name), data_name(data_name),
      dev_fd(-1),
      data_fd(-1)
{

    if (data_name) {
        data_fd = openInput(data_name);
    }
	fifo_fd = -1;
	fifo_name = NULL;
	mBatchEnabled = false;
}

SensorBase::SensorBase(
	const char* dev_name,
	const char* data_name,
	const char* fifo_name)
	: dev_name(dev_name), data_name(data_name),fifo_name(fifo_name),
     dev_fd(-1),
     data_fd(-1),
     fifo_fd(-1)
{
	if (data_name) {
        data_fd = openInput(data_name);
    }
	if(fifo_name){
		open_fifo_device();
	}
	mBatchEnabled = false;
}

SensorBase::~SensorBase() {
    if (data_fd >= 0) {
        close(data_fd);
    }
    if (dev_fd >= 0) {
        close(dev_fd);
    }
	if(fifo_fd >= 0)
	{
		close(fifo_fd);
	}
}

int SensorBase::open_device() {
    if (dev_fd<0 && dev_name) {
        dev_fd = open(dev_name, O_RDONLY);
        ALOGE_IF(dev_fd<0, "Couldn't open %s (%s)", dev_name, strerror(errno));
    }
    return 0;
}

int SensorBase::close_device() {
    if (dev_fd >= 0) {
        close(dev_fd);
        dev_fd = -1;
    }
    return 0;
}

int SensorBase::open_fifo_device(){
	if (fifo_fd < 0 && fifo_name) {
        fifo_fd = open(fifo_name, O_RDONLY);
        ALOGE_IF(fifo_fd < 0, "Couldn't  open %s (%s)", fifo_name, strerror(errno));
    }
	return 0;
}
int SensorBase::close_fifo_device(){
	if (fifo_fd >= 0) {
        close(fifo_fd);
        fifo_fd = -1;
    }
    return 0;
}
int SensorBase::getFd() const 
{    
	if(mBatchEnabled){
		return fifo_fd;
	}else{
		return data_fd;
	}
}
int SensorBase::setEnable(int32_t handle, int enabled)
{
	return 0;
}
int SensorBase::getEnable(int32_t handle)
{
	return 0;
}

int SensorBase::setDelay(int32_t handle, int64_t ns) {
    return 0;
}

bool SensorBase::hasPendingEvents() const {
	return false;
}
void  processEvent(int code, int value) 
{

}

int64_t SensorBase::getTimestamp() {
    struct timespec t;
    t.tv_sec = t.tv_nsec = 0;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return int64_t(t.tv_sec)*1000000000LL + t.tv_nsec;
}

int SensorBase::openInput(const char* inputName) {
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
    ALOGE_IF(fd<0, "couldn't find '%s' input device", inputName);
    return fd;
}
int SensorBase::readEvents(sensors_event_t* data, int count)
{
	return 0;
}
int SensorBase::batch(int handle, int flags, int64_t period_ns, int64_t timeout){
	/*default , not support batch mode or SENSORS_BATCH_WAKE_UPON_FIFO_FULL */
	if(timeout > 0 || flags & SENSORS_BATCH_WAKE_UPON_FIFO_FULL)
		return -EINVAL;
	if(!(flags & SENSORS_BATCH_DRY_RUN)){
		setDelay(handle,period_ns);
	}
	return 0;
}
int SensorBase::flush(int handle){
	return  -EINVAL;
}


