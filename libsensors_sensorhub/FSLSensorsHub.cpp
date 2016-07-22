/*
 * Copyright (C) 2012-2015 Freescale Semiconductor, Inc.
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
#include "FSLSensorsHub.h"

#define FSL_SENS_CTRL_NAME   	"FSL_SENS_HUB"
#define FSL_SENS_DATA_NAME    	"FSL_SENS_HUB" 
#define FSL_SENS_SYSFS_PATH   	"/sys/class/misc"
#define FSL_SENS_SYSFS_DELAY  	"poll_delay"
#define FSL_SENS_SYSFS_ENABLE 	"enable"
#define FSL_ACC_DEVICE_NAME	    "FreescaleAccelerometer"
#define FSL_MAG_DEVICE_NAME 	"FreescaleMagnetometer"
#define FSL_GYRO_DEVICE_NAME	"FreescaleGyroscope"

#define EVENT_ACC_X			REL_X
#define EVENT_ACC_Y	   		REL_Y
#define EVENT_ACC_Z			REL_Z		

#define EVENT_MAG_X			REL_RX
#define EVENT_MAG_Y			REL_RY
#define EVENT_MAG_Z			REL_RZ
#define EVENT_MAG_STATUS	REL_MISC

#define EVENT_GYRO_X		(REL_MISC + 1)	/*0x0A*/
#define EVENT_GYRO_Y		(REL_MISC + 2)	/*0x0B*/
#define EVENT_GYRO_Z		(REL_MISC + 3)	/*0x0C*/

#define EVENT_STEP_DETECTED	 	REL_DIAL	
#define EVENT_STEP_COUNT_HIGH	REL_HWHEEL 
#define EVENT_STEP_COUNT_LOW	REL_WHEEL  

#define EVENT_ORNT_X		ABS_X 
#define EVENT_ORNT_Y		ABS_Y 
#define EVENT_ORNT_Z		ABS_Z 
#define EVENT_ORNT_STATUS	EVENT_MAG_STATUS 	/*0x0A*/

#define EVENT_LINEAR_ACC_X	ABS_RX 
#define EVENT_LINEAR_ACC_Y	ABS_RY 
#define EVENT_LINEAR_ACC_Z	ABS_RZ 

#define EVENT_GRAVITY_X		ABS_HAT0X
#define EVENT_GRAVITY_Y		ABS_HAT1X
#define EVENT_GRAVITY_Z		ABS_HAT2X

#define EVENT_ROTATION_VECTOR_W		ABS_MISC
#define EVENT_ROTATION_VECTOR_A		ABS_HAT0Y
#define EVENT_ROTATION_VECTOR_B		ABS_HAT1Y
#define EVENT_ROTATION_VECTOR_C		ABS_HAT2Y



#define ACC_DATA_CONVERSION(value)  ((float)value * GRAVITY_EARTH/0x4000  )
#define MAG_DATA_CONVERSION(value)  ((float)value/10.0f)
#define ORNT_DATA_CONVERSION(value) ((float)value/10.0f)
#define GYRO_DATA_CONVERSION(value) ((float)value/1000.0f /180.0f * M_PI )
#define RV_DATA_CONVERSION(value)   ((float)value /10000.0f)
#define LA_DATA_CONVERSION(value)   ((float)value /10.0f)
#define GRAVT_DATA_CONVERSION(value)((float)value /10.0f)



FSLSensorsHub::FSLSensorsHub()
: SensorBase(FSL_SENS_CTRL_NAME, FSL_SENS_DATA_NAME),
  mPendingMask(0),
  mInputReader(16)
{
    memset(&mPendingEvent[0], 0, sensors *sizeof(sensors_event_t));

	mEnabled[accel] = 0;
	mDelay[accel] = 0;
	mPendingEvent[accel].version = sizeof(sensors_event_t);
    mPendingEvent[accel].sensor  = ID_A;
    mPendingEvent[accel].type    = SENSOR_TYPE_ACCELEROMETER;
    mPendingEvent[accel].magnetic.status = SENSOR_STATUS_ACCURACY_LOW;
    mPendingEvent[accel].version = sizeof(sensors_event_t);

	mEnabled[mag] = 0;
	mDelay[mag] = 0;
    mPendingEvent[mag].version = sizeof(sensors_event_t);
    mPendingEvent[mag].sensor  = ID_M;
    mPendingEvent[mag].type    = SENSOR_TYPE_MAGNETIC_FIELD;
    mPendingEvent[mag].magnetic.status = SENSOR_STATUS_ACCURACY_LOW;
    mPendingEvent[mag].version = sizeof(sensors_event_t);

	mEnabled[orn] = 0;
	mDelay[orn] = 0;
    mPendingEvent[orn].sensor  = ID_O;
    mPendingEvent[orn].type    = SENSOR_TYPE_ORIENTATION;
    mPendingEvent[orn].orientation.status = SENSOR_STATUS_ACCURACY_LOW;
	mPendingEvent[orn].version = sizeof(sensors_event_t);

	mEnabled[gyro] = 0;
	mDelay[gyro] = 0;
    mPendingEvent[gyro].sensor  = ID_GY;
    mPendingEvent[gyro].type    = SENSOR_TYPE_GYROSCOPE;
    mPendingEvent[gyro].orientation.status = SENSOR_STATUS_ACCURACY_LOW;
	mPendingEvent[gyro].version = sizeof(sensors_event_t);

	mEnabled[rv] = 0;
	mDelay[rv] = 0;
    mPendingEvent[rv].sensor  = ID_RV;
    mPendingEvent[rv].type    = SENSOR_TYPE_ROTATION_VECTOR;
    mPendingEvent[rv].orientation.status = SENSOR_STATUS_ACCURACY_LOW;
	mPendingEvent[rv].version = sizeof(sensors_event_t);

	mEnabled[la] = 0;
	mDelay[la] = 0;
    mPendingEvent[la].sensor  = ID_LA;
    mPendingEvent[la].type    = SENSOR_TYPE_LINEAR_ACCELERATION;
    mPendingEvent[la].orientation.status = SENSOR_STATUS_ACCURACY_LOW;
	mPendingEvent[la].version = sizeof(sensors_event_t);

	mEnabled[gravt] = 0;
	mDelay[gravt] = 0;
    mPendingEvent[gravt].sensor  = ID_GR;
    mPendingEvent[gravt].type    = SENSOR_TYPE_GRAVITY;
    mPendingEvent[gravt].orientation.status = SENSOR_STATUS_ACCURACY_LOW;
	mPendingEvent[gravt].version = sizeof(sensors_event_t);
	
	mEnabled[sd] = 0;
	mDelay[sd] = 0;
    mPendingEvent[sd].sensor  = ID_SD;
    mPendingEvent[sd].type    = SENSOR_TYPE_STEP_DETECTOR;
    mPendingEvent[sd].orientation.status = SENSOR_STATUS_ACCURACY_HIGH;
	mPendingEvent[sd].version = sizeof(sensors_event_t);

	mEnabled[sc] = 0;
	mDelay[sc] = 0;
    mPendingEvent[sc].sensor  = ID_SC;
    mPendingEvent[sc].type    = SENSOR_TYPE_STEP_COUNTER;
    mPendingEvent[sc].orientation.status = SENSOR_STATUS_ACCURACY_LOW;
	mPendingEvent[sc].version = sizeof(sensors_event_t);

	sprintf(mClassPath[accel],"%s/%s",FSL_SENS_SYSFS_PATH,FSL_ACC_DEVICE_NAME);
	sprintf(mClassPath[mag],"%s/%s",FSL_SENS_SYSFS_PATH,FSL_MAG_DEVICE_NAME);
	sprintf(mClassPath[gyro],"%s/%s",FSL_SENS_SYSFS_PATH,FSL_GYRO_DEVICE_NAME);

}

FSLSensorsHub::~FSLSensorsHub()
{
}

int FSLSensorsHub::setEnable(int32_t handle, int en)
{
	int err = 0;
	int what = accel;
	bool isHaveSensorRun = 0;
    switch(handle){
		case ID_A : what = accel; break;
		case ID_M : what = mag;   break;
		case ID_O : what = orn;   break;
		case ID_GY: what = gyro;  break; 
		case ID_RV: what = rv;	  break;
		case ID_LA: what = la;	  break;
		case ID_GR: what = gravt; break;
		case ID_SD: what = sd;    break;
		case ID_SC: what = sc;    break;
		
    }

    if(en)
		mEnabled[what]++;
	else
		mEnabled[what]--;
	
	if(mEnabled[what] < 0)
		mEnabled[what] = 0;

	for(int i = 0; i < sensors; i++ ){
		if(mEnabled[i] > 0)
		{
			isHaveSensorRun = 1;
			break;
		}
	}
	if(isHaveSensorRun){
		if(mEnabled[rv] > 0 || mEnabled[gravt] > 0 ||mEnabled[la] > 0 || mEnabled[mag]> 0  || mEnabled[orn] > 0 || mEnabled[gyro]> 0) //need fusion run
		{
			enable_sensor(accel);
			enable_sensor(mag);
			enable_sensor(gyro);
		}else if(mEnabled[accel] > 0)  //only accel enable
		{ 
			enable_sensor(accel);
			disable_sensor(mag);
			disable_sensor(gyro);
		}
	}else
	{
		disable_sensor(accel);
		disable_sensor(mag);
		disable_sensor(gyro);
	}
	ALOGD("FSLSensorsHub sensor waht = %d , enable = %d",what,mEnabled[what]);
    return err;
}

int FSLSensorsHub::setDelay(int32_t handle, int64_t ns)
{
    if (ns < 0)
        return -EINVAL;
	int what = accel;
    switch(handle){
		case ID_A : what = accel; break;
		case ID_M : what = mag;   break;
		case ID_O : what = orn;   break;
		case ID_GY: what = gyro;  break; 
		case ID_RV: what = rv;	  break;
		case ID_LA: what = la;	  break;
		case ID_GR: what = gravt; break;
		case ID_SD: what = sd;	  break;
		case ID_SC: what = sc;    break;
    }

    mDelay[what] = ns;
	if(what == accel)
		update_delay(accel,mDelay[accel]);
	else if(what == mag || what == orn)
		update_delay(mag,mDelay[mag]);
	else if(what == gyro)
		update_delay(gyro,mDelay[gyro]);
	else{
		update_delay(accel,mDelay[accel]);
		update_delay(mag,mDelay[mag]);
		update_delay(gyro,mDelay[gyro]);
	}
    return 0;
}

int FSLSensorsHub::update_delay(int sensor_type , int64_t ns)
{
    return writeDelay(sensor_type,ns);
}

int FSLSensorsHub::readEvents(sensors_event_t* data, int count)
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
        if ((type == EV_ABS) || (type == EV_REL) || (type == EV_KEY)) {
            processEvent(type,event->code, event->value);
            mInputReader.next();
        } else if (type == EV_SYN) {
            int64_t time = timevalToNano(event->time);
			for(i = 0 ; i< sensors && mPendingMask && count ;i++){
			  	 	if(mPendingMask & (1 << i)){
						mPendingMask &= ~(1 << i);
						mPendingEvent[i].timestamp = time;
						if (mEnabled[i]) {
							*data++ = mPendingEvent[i];
							count--;
							numEventReceived++;
						}
			  	 	}
	       }
		   if (!mPendingMask) {
		       mInputReader.next();
		   }
        } else {
            mInputReader.next();
        }
    }

    return numEventReceived;
}

void FSLSensorsHub::processEvent(int code, int value)
{

}
void FSLSensorsHub::processEvent(int type ,int code, int value){
	static uint64_t steps_high = 0,steps_low = 0;
	if(type == EV_REL){
		 switch (code) {
		 case EVENT_ACC_X :
            mPendingMask |= 1 << accel;
            mPendingEvent[accel].acceleration.x = ACC_DATA_CONVERSION(value);
            break;
        case EVENT_ACC_Y:
            mPendingMask |= 1 << accel;
            mPendingEvent[accel].acceleration.y = ACC_DATA_CONVERSION(value);
            break;
        case EVENT_ACC_Z:
            mPendingMask |=  1 << accel;
            mPendingEvent[accel].acceleration.z = ACC_DATA_CONVERSION(value);
            break;
        case EVENT_MAG_X :
            mPendingMask |= 1 << mag;
            mPendingEvent[mag].magnetic.x = MAG_DATA_CONVERSION(value);
            break;
        case EVENT_MAG_Y:
            mPendingMask |= 1 << mag;
            mPendingEvent[mag].magnetic.y = MAG_DATA_CONVERSION(value);
            break;
        case EVENT_MAG_Z:
            mPendingMask |=  1 << mag;
            mPendingEvent[mag].magnetic.z = MAG_DATA_CONVERSION(value);
            break;
		case EVENT_MAG_STATUS:
			mPendingMask |=  1 << mag;
			mPendingEvent[mag].magnetic.status 	= value;
			mPendingEvent[orn].orientation.status	= value;
			break;
		case EVENT_GYRO_X :
            mPendingMask |=  1 << gyro;
            mPendingEvent[gyro].gyro.x= GYRO_DATA_CONVERSION(value);
            break;
        case EVENT_GYRO_Y :
            mPendingMask |=  1 << gyro;
            mPendingEvent[gyro].gyro.y = GYRO_DATA_CONVERSION(value);
            break;
        case EVENT_GYRO_Z :
            mPendingMask |=  1 << gyro;
            mPendingEvent[gyro].gyro.z	= GYRO_DATA_CONVERSION(value);
            break;
		case EVENT_STEP_DETECTED:
            mPendingMask |=  1 << sd;
            mPendingEvent[sd].data[0] = 1.0f;
            break;
		case EVENT_STEP_COUNT_HIGH:
			steps_high = (uint64_t)(value & 0xffffffff);
            break;
		case EVENT_STEP_COUNT_LOW:
            mPendingMask |=  1 << sc;
			steps_low = (uint64_t)(value & 0xffffffff);
            mPendingEvent[sc].u64.step_counter = ((steps_high << 32) | steps_low);
            break;
		}
    
	}else if(type == EV_ABS){
	 switch(code){
	 	case EVENT_ORNT_X :
            mPendingMask |=  1 << orn;
            mPendingEvent[orn].orientation.azimuth	= ORNT_DATA_CONVERSION(value);
            break;
        case EVENT_ORNT_Y :
            mPendingMask |=  1 << orn;
            mPendingEvent[orn].orientation.pitch = ORNT_DATA_CONVERSION(value);
            break;
        case EVENT_ORNT_Z :
            mPendingMask |=  1 << orn;
            mPendingEvent[orn].orientation.roll	= ORNT_DATA_CONVERSION(value);
            break;
		case EVENT_LINEAR_ACC_X :
            mPendingMask |= 1 << la;
            mPendingEvent[la].data[0] = LA_DATA_CONVERSION(value);
            break;
        case EVENT_LINEAR_ACC_Y:
            mPendingMask |= 1 << la;
            mPendingEvent[la].data[1] = LA_DATA_CONVERSION(value);
            break;
        case EVENT_LINEAR_ACC_Z:
            mPendingMask |=  1 << la;
            mPendingEvent[la].data[2] = LA_DATA_CONVERSION(value);
            break;

		case EVENT_GRAVITY_X :
            mPendingMask |= 1 << gravt;
            mPendingEvent[gravt].data[0] = GRAVT_DATA_CONVERSION(value);
            break;
        case EVENT_GRAVITY_Y:
            mPendingMask |= 1 << gravt;
            mPendingEvent[gravt].data[1] = GRAVT_DATA_CONVERSION(value);
            break;
        case EVENT_GRAVITY_Z:
            mPendingMask |=  1 << gravt;
            mPendingEvent[gravt].data[2] = GRAVT_DATA_CONVERSION(value);
            break;
			
		case EVENT_ROTATION_VECTOR_W :
            mPendingMask |= 1 << rv;
            mPendingEvent[rv].data[3] = RV_DATA_CONVERSION(value);
            break;
        case EVENT_ROTATION_VECTOR_A:
            mPendingMask |= 1 << rv;
            mPendingEvent[rv].data[0] = RV_DATA_CONVERSION(value);
            break;
        case EVENT_ROTATION_VECTOR_B:
            mPendingMask |=  1 << rv;
            mPendingEvent[rv].data[1] = RV_DATA_CONVERSION(value);
            break;
		 case EVENT_ROTATION_VECTOR_C:
            mPendingMask |=  1 << rv;
            mPendingEvent[rv].data[2] = RV_DATA_CONVERSION(value);
            break;
	  }
	}
}


int FSLSensorsHub::writeEnable(int what ,int isEnable) {
	char attr[PATH_MAX] = {'\0'};
	int err = 0;
	if(mClassPath[0] == NULL)
		return -1;

	strcpy(attr, mClassPath[what]);
	strcat(attr,"/");
	strcat(attr,FSL_SENS_SYSFS_ENABLE);

	int fd = open(attr, O_RDWR);
	if (0 > fd) {
		ALOGE("Could not open (write-only) SysFs attribute \"%s\" (%s).", attr, strerror(errno));
		return -errno;
	}

	char buf[16];
	sprintf(buf,"%d",isEnable);
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

int FSLSensorsHub::writeDelay(int what,int64_t ns) {
	char attr[PATH_MAX] = {'\0'};
	int delay;
	if(mClassPath[0] == NULL)
		return -1;

	strcpy(attr, mClassPath[what]);
	strcat(attr,"/");
	strcat(attr,FSL_SENS_SYSFS_DELAY);

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
	delay = ns/1000/1000;
    sprintf(buf, "%d",delay);
	ALOGD("FSL_SENS write delay %s\n",buf);
    write(fd, buf, strlen(buf)+1);
    close(fd);
    return 0;

}

int FSLSensorsHub::enable_sensor(int what) {
	return writeEnable(what,1);
}

int FSLSensorsHub::disable_sensor(int what) {
	return writeEnable(what,0);
}
int FSLSensorsHub::getEnable(int32_t handle) {
	int what = accel;
	switch(handle){
		case ID_A : what = accel; break;
		case ID_M : what = mag;   break;
		case ID_O : what = orn;   break;
		case ID_GY: what = gyro;  break; 
		case ID_RV: what = rv;	  break;
		case ID_LA: what = la;	  break;
		case ID_GR: what = gravt; break;
		case ID_SD: what = sd;	  break;
		case ID_SC: what = sc;    break;
    }

	return mEnabled[what];
}

/*****************************************************************************/

