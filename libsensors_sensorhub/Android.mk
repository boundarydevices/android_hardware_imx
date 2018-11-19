# Copyright (C) 2008 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
ifeq ($(findstring imx, $(TARGET_BOARD_PLATFORM)), imx)

ifeq ($(BOARD_USE_SENSOR_FUSION),true)
    COMPILE_SENSOR_HUB = true
endif

ifeq ($(BOARD_USE_SENSOR_FUSION_64BIT),true)
    COMPILE_SENSOR_HUB = true
endif

LOCAL_PATH := $(call my-dir)

ifeq ($(COMPILE_SENSOR_HUB),true)

ifneq ($(TARGET_SIMULATOR),true)

# HAL module implemenation, not prelinked, and stored in
# hw/<SENSORS_HARDWARE_MODULE_ID>.<ro.product.board>.so
include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE := sensors.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_TAGS := eng

LOCAL_VENDOR_MODULE := true
LOCAL_SRC_FILES :=                      \
            sensors.cpp                 \
            SensorBase.cpp              \
            InputEventReader.cpp

LOCAL_C_INCLUDES += hardware/libhardware/include \
                    system/core/include

ifeq ($(BOARD_USE_LEGACY_SENSOR),true)
    LOCAL_CFLAGS += -DCONFIG_LEGACY_SENSOR
    LOCAL_SRC_FILES += FSLSensorsHub.cpp
    LOCAL_SRC_FILES += PressSensor.cpp
    LOCAL_SRC_FILES += LightSensor.cpp
endif
ifeq ($(BOARD_USE_SENSOR_PEDOMETER),true)
    LOCAL_CFLAGS += -DCONFIG_SENSOR_PEDOMETER
    LOCAL_SRC_FILES += Stepdetector.cpp
    LOCAL_SRC_FILES += Stepcounter.cpp
endif

LOCAL_SHARED_LIBRARIES := liblog libcutils libdl

include $(BUILD_SHARED_LIBRARY)

endif # !TARGET_SIMULATOR
endif # COMPILE_SENSOR_HUB
endif
