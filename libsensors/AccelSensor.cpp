/*
 * Copyright (C) 2012-2014 Freescale Semiconductor, Inc.
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
#include <stdlib.h>
#include <poll.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/select.h>
#include <dlfcn.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include "AccelSensor.h"
#define ACC_DATA_NAME    "FreescaleAccelerometer" 
#define ACC_FIFO_NAME    "/dev/mma8x5x"
#define ACC_SYSFS_PATH   "/sys/class/input"
#define ACC_SYSFS_DELAY  "poll_delay"
#define ACC_SYSFS_ENABLE "enable"
#define ACC_SYSFS_FIFO	 "fifo"

#define ACC_EVENT_X ABS_X
#define ACC_EVENT_Y ABS_Y
#define ACC_EVENT_Z ABS_Z
#define ACC_DATA_CONVERSION(value) (float)((float)((int)(value)) * (GRAVITY_EARTH / (0x4000)))
#define ACC_FIFO_MAX_TIMEOUT	20480000000LL		//(640 * ACC_FIFO_LEN *1000000) ns
#define ACC_FIFO_MIN_TIMEOUT	   40000000LL		//(1.25 * ACC_FIFO_LEN *1000000)     ns

AccelSensor::AccelSensor()
: SensorBase(NULL, ACC_DATA_NAME,ACC_FIFO_NAME),
      mEnabled(0),
      mPendingMask(0),
      mInputReader(4),
      mFifoCount(0),
      mDelay(0)
{
	mBatchEnabled = 0;
	mUser = 0;
    memset(&mPendingEvent, 0, sizeof(mPendingEvent));
	memset(mClassPath, '\0', sizeof(mClassPath));
	
    mPendingEvent.version = sizeof(sensors_event_t);
    mPendingEvent.sensor  = ID_A;
    mPendingEvent.type    = SENSOR_TYPE_ACCELEROMETER;
    mPendingEvent.acceleration.status = SENSOR_STATUS_ACCURACY_HIGH;
	if(sensor_get_class_path(mClassPath))
	{
		ALOGE("Can`t find the Acc sensor!");
	}
}

AccelSensor::~AccelSensor()
{
}

int AccelSensor::setEnable(int32_t handle, int en)
{
	int err = 0;
    uint32_t newState  = en;
	if(handle != ID_A && handle != ID_O && handle != ID_M)
		return -1;
	/*mUser to control is sensor enable
         *mEnable to control is Acc to report data
	*/
	if(en){
		mUser++;
		if(handle == ID_A)
			mEnabled++;
	}
	else{
		mUser--;
		if(handle == ID_A)
			mEnabled--;
	}
	if(mUser < 0)
		mUser = 0;
	if(mEnabled < 0)
		mEnabled = 0;
	if(mUser)
		err = enable_sensor();
	else
		err = disable_sensor();
	ALOGD("AccelSensor enable %d , handle %d ,mEnabled %d",en , handle ,mEnabled);
    return err;
}

int AccelSensor::setDelay(int32_t handle, int64_t ns)
{
    if (ns < 0)
        return -EINVAL;

    mDelay = ns;
    return update_delay();
}
bool AccelSensor::hasPendingEvents(){
	return (mFifoCount > 0 || (mFlushed & (0x01 << ID_A)));
}
int AccelSensor::update_delay()
{
    return set_delay(mDelay);
}
/*
struct mma8x5x_data_axis {
	short x;
	short y;
	short z;
};
struct mma8x5x_fifo{
	int count;
	int64_t period;
	int64_t timestamp;
	struct mma8x5x_data_axis fifo_data[MMA8X5X_FIFO_SIZE];
};
*/
int AccelSensor::read_fifo(){
	char buf[256];
	int i = 0;
	int nread = 0,n;
	int count;
	int64_t period;
	int64_t timestamp;
	int offset = 0;
	int16_t * data;
	int16_t axis;
	struct pollfd mPollFds[1];
	if(fifo_fd){
		mPollFds[0].fd = fifo_fd;
		mPollFds[0].events = POLLIN;
		mPollFds[0].revents = 0;
		memset(buf,0,sizeof(buf));
		n = poll(mPollFds, 1, 0);  //re-check if fifo is ready
		if(n <= 0){
			mFifoCount = 0;
			return -EINVAL;
		}
		nread = read(fifo_fd, buf,sizeof(buf));
		if(nread > 0){
			mFifoCount = 0;
			offset = 0;
			count = *((int *)(&buf[offset]));
			if(count > 0 && count <= MMA8X5X_FIFO_SIZE){
				offset += sizeof(count);
				period = *((int64_t *)(&buf[offset]));
				offset += sizeof(period);
				timestamp = *((int64_t *)(&buf[offset]));
				offset += sizeof(timestamp);
				data = ((int16_t *)(&buf[offset]));
				for(i = 0 ; i <  count; i++){
					axis = *data++;
					mFifoPendingEvent[i].acceleration.x = ACC_DATA_CONVERSION(axis);
					axis = *data++;
					mFifoPendingEvent[i].acceleration.y = ACC_DATA_CONVERSION(axis);
					axis = *data++;
					mFifoPendingEvent[i].acceleration.z = ACC_DATA_CONVERSION(axis);
					mFifoPendingEvent[i].timestamp = timestamp - (count - i -1)* period;
					mFifoPendingEvent[i].sensor  = ID_A;
					mFifoPendingEvent[i].type    = SENSOR_TYPE_ACCELEROMETER;
					mFifoPendingEvent[i].acceleration.status = SENSOR_STATUS_ACCURACY_HIGH;
				}
				mFifoCount = count;
			}
		}
	}
	return nread;
}
int AccelSensor::readEvents(sensors_event_t* data, int count)
{
	int events = 0;
	int clockid = CLOCK_MONOTONIC;
	int ret;
    if (count < 1)
        return -EINVAL;
	int numEventReceived = 0;
	sensors_event_t sensor_event;
	if(mBatchEnabled & (0x01 << ID_A)){
		/*read the batch FIFO infomation,then add the META_DATA_VERSION event at the end of FIFO */
		if(mFifoCount <= 0){  
			if(mFlushed & (0x01 << ID_A)){/*if batch mode enable and fifo lenght is zero */
				memset(&sensor_event,0,sizeof(sensor_event));
				sensor_event.version = META_DATA_VERSION;
				sensor_event.type = SENSOR_TYPE_META_DATA;
				sensor_event.meta_data.sensor = ID_A;
				sensor_event.meta_data.what = 0;
				*data++ = sensor_event;
				count--;
				numEventReceived++;
				mFlushed &= ~(0x01 << ID_A);;
			}
			ret = read_fifo();
			if(ret <= 0)  /*not fifo data ,return immediately*/
				return 0;
			}
			events = (count -1 < mFifoCount)? count -1 : mFifoCount;
			if(events){
				memcpy(data,&mFifoPendingEvent[MMA8X5X_FIFO_SIZE - events],sizeof(sensors_event_t) *events);
				mFifoCount -= events;
				numEventReceived += events;
				data += events;
			}
			if(count > 0 && mFifoCount == 0){  //end fifo flag
				memset(&sensor_event,0,sizeof(sensor_event));
				sensor_event.version = META_DATA_VERSION;
				sensor_event.type = SENSOR_TYPE_META_DATA;
				sensor_event.meta_data.sensor = ID_A;
				sensor_event.meta_data.what = 0;
				*data++ = sensor_event;
				count--;
				numEventReceived++;
			}
	}else{
		if(mFlushed & (0x01 << ID_A)){ /*if batch mode disable , send flush META_DATA_FLUSH_COMPLETE  immediately*/
			memset(&sensor_event,0,sizeof(sensor_event));
			sensor_event.version = META_DATA_VERSION;
			sensor_event.type = SENSOR_TYPE_META_DATA;
			sensor_event.meta_data.sensor = ID_A;
			sensor_event.meta_data.what = 0;
			*data++ = sensor_event;
			count--;
			numEventReceived++;
			mFlushed &= ~(0x01 << ID_A);;
		}

    if (TEMP_FAILURE_RETRY(ioctl(data_fd, EVIOCSCLOCKID, &clockid)) < 0) {
        ALOGW("Could not set input clock id to CLOCK_MONOTONIC. errno=%d", errno);
    }

		ssize_t n = mInputReader.fill(data_fd);
		if (n < 0)
			return n;
		input_event const* event;
        while (count && mInputReader.readEvent(&event)) {
            int type = event->type;
            if ((type == EV_ABS) || (type == EV_REL) || (type == EV_KEY)) {
                processEvent(event->code, event->value);
                mInputReader.next();
            } else if (type == EV_SYN) {
                int64_t time = timevalToNano(event->time);
                if (mPendingMask) {
                    mPendingMask = 0;
                    mPendingEvent.timestamp = time;
                    if (mEnabled) {
                        *data++ = mPendingEvent;
                        count--;
                        numEventReceived++;
                    }
                }
                if (!mPendingMask) {
                    mInputReader.next();
	            }
	        } else {
	            ALOGE("AccelSensor: unknown event (type=%d, code=%d)",
	                    type, event->code);
	            mInputReader.next();
	        }
	    }
	}
	return numEventReceived;
}

void AccelSensor::processEvent(int code, int value)
{

    switch (code) {
        case ACC_EVENT_X :
            mPendingMask = 1;
            mPendingEvent.acceleration.x = ACC_DATA_CONVERSION(value);
            break;
        case ACC_EVENT_Y :
            mPendingMask = 1;
            mPendingEvent.acceleration.y = ACC_DATA_CONVERSION(value);
            break;
        case ACC_EVENT_Z :
            mPendingMask = 1;
            mPendingEvent.acceleration.z = ACC_DATA_CONVERSION(value);
            break;
    }
}

int AccelSensor::writeEnable(int isEnable) {
	char attr[PATH_MAX] = {'\0'};
	if(mClassPath[0] == '\0')
		return -1;

	strcpy(attr, mClassPath);
	strcat(attr,"/");
	strcat(attr,ACC_SYSFS_ENABLE);

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

int AccelSensor::writeDelay(int64_t ns) {
	char attr[PATH_MAX] = {'\0'};
	if(mClassPath[0] == '\0')
		return -1;

	strcpy(attr, mClassPath);
	strcat(attr,"/");
	strcat(attr,ACC_SYSFS_DELAY);

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

int AccelSensor::enable_sensor() {
	return writeEnable(1);
}

int AccelSensor::disable_sensor() {
	return writeEnable(0);
}

int AccelSensor::set_delay(int64_t ns) {
	return writeDelay(ns);
}

int AccelSensor::getEnable(int32_t handle) {
	return (handle == ID_A) ? mEnabled : 0;
}

int AccelSensor::sensor_get_class_path(char *class_path)
{
	char dirname[] = ACC_SYSFS_PATH;
	char buf[256];
	int res;
	DIR *dir;
	struct dirent *de;
	int fd = -1;
	int found = 0;

	dir = opendir(dirname);
	if (dir == NULL)
		return -1;

	while((de = readdir(dir))) {
		if (strncmp(de->d_name, "input", strlen("input")) != 0) {
		    continue;
        	}

		sprintf(class_path, "%s/%s", dirname, de->d_name);
		snprintf(buf, sizeof(buf), "%s/name", class_path);

		fd = open(buf, O_RDONLY);
		if (fd < 0) {
		    continue;
		}
		if ((res = read(fd, buf, sizeof(buf))) < 0) {
		    close(fd);
		    continue;
		}
		buf[res - 1] = '\0';
		if (strcmp(buf, ACC_DATA_NAME) == 0) {
		    found = 1;
		    close(fd);
		    break;
		}

		close(fd);
		fd = -1;
	}
	closedir(dir);
	//ALOGE("the G sensor dir is %s",class_path);

	if (found) {
		return 0;
	}else {
		*class_path = '\0';
		return -1;
	}
}
int AccelSensor::fifo(int64_t period_ns,int64_t timeout_ns,int wakeup){
	char attr[PATH_MAX] = {'\0'};
	char buf[256];
	int timeout_ms = timeout_ns /1000000;
	int period_ms = period_ns /1000000;
	if(mClassPath[0] == '\0')
		return -1;
	strcpy(attr, mClassPath);
	strcat(attr,"/");
	strcat(attr,ACC_SYSFS_FIFO);
	int fd = open(attr, O_RDWR);
	if (0 > fd) {
		ALOGE("Could not open (write-only) SysFs attribute \"%s\" (%s).", attr, strerror(errno));
		return -errno;
	}
	sprintf(buf,"%d,%d,%d",period_ms,timeout_ms,(wakeup > 0 ? 1 : 0));
	int err = 0;
	err = write(fd,buf,strlen(buf) + 1);
	if (0 > err) {
		err = -errno;
		ALOGE("Could not write SysFs attribute \"%s\" (%s).", attr, strerror(errno));
	} else {
		err = 0;
	}
	close(fd);
	return 0;
}
int AccelSensor::batch(int handle, int flags, int64_t period_ns, int64_t timeout){
	int wakeup;
	if(flags & SENSORS_BATCH_DRY_RUN){
		if(timeout > ACC_FIFO_MAX_TIMEOUT)
			return -EINVAL;
		else
			return 0;
	}else{
		wakeup = 0;
		if(flags & SENSORS_BATCH_WAKE_UPON_FIFO_FULL)
			wakeup = 1;
		fifo(period_ns,timeout,wakeup);
		mDelay = period_ns;
		if(timeout > 0)
			mBatchEnabled |= (0x01 << ID_A);
		else
			mBatchEnabled &= ~(0x01 << ID_A);
	}
	return 0;
}
int AccelSensor::flush(int handle){
	if(mEnabled){
		mFlushed |= (0x01 << ID_A);
		return 0;
	}else{
		return -EINVAL;
	}
}

/*****************************************************************************/

