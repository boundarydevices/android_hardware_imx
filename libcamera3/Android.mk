# Copyright (C) 2012 The Android Open Source Project
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

LOCAL_PATH := $(call my-dir)

ifeq ($(BOARD_HAVE_IMX_CAMERA),true)

include $(CLEAR_VARS)

LOCAL_MODULE := camera.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_RELATIVE_PATH := hw

LOCAL_VENDOR_MODULE := true
LOCAL_C_INCLUDES += \
    $(IMX_LIB_PATH)/imx-lib/pxp \
    system/core/include \
    system/media/camera/include \
    external/jpeg \
    $(FSL_PROPRIETARY_PATH)/fsl-proprietary/include \
    device/boundary/common/kernel-headers \
    $(IMX_PATH)/imx/include \
    $(FSL_IMX_OMX_PATH)/fsl_imx_omx/OpenMAXIL/src/component/vpu_wrapper \
    vendor/nxp/vpu_wrapper \
    $(IMX_PATH)/imx/display/display \
    $(IMX_PATH)/imx/opencl-2d \
    system/core/libion/include \
    hardware/libhardware/include \
    frameworks/av/include \
    frameworks/native/include

LOCAL_SRC_FILES := \
    CameraHAL.cpp \
    Camera.cpp \
    Metadata.cpp \
    Stream.cpp \
    VendorTags.cpp \
    CameraUtils.cpp \
    MessageQueue.cpp \
    VideoStream.cpp \
    JpegBuilder.cpp \
    ImxCamera.cpp \
    YuvToJpegEncoder.cpp \
    NV12_resize.cpp \
    USPStream.cpp \
    DMAStream.cpp \
    UvcDevice.cpp \
    HwJpegEncoder.cpp \
    MMAPStream.cpp \
    TinyExif.cpp \
    ImageProcess.cpp \
    CameraMetadata.cpp \
    utils/CameraConfigurationParser.cpp \
    ISPCamera.cpp

LOCAL_SHARED_LIBRARIES := \
    libcamera_metadata \
    libcutils \
    liblog \
    libsync \
    libutils \
    libc \
    libjpeg \
    libion \
    libbinder \
    libhardware_legacy \
    libjsoncpp \
    libbase \
    libfsldisplay

LOCAL_WHOLE_STATIC_LIBRARIES := libionallocator

ifeq ($(BOARD_HAVE_VPU),true)
ifneq ($(BOARD_SOC_TYPE), IMX8Q)
    LOCAL_SHARED_LIBRARIES += \
            lib_vpu_wrapper
    LOCAL_CFLAGS += -DBOARD_HAVE_VPU
endif

#Enable UVC camera with mjpeg streaming
#So far only i.mx6 has mjpeg decoder for this UVC
ifeq ($(BOARD_HAVE_USB_MJPEG_CAMERA), true)
    LOCAL_SRC_FILES += \
        MJPGStream.cpp
endif
endif

ifeq ($(HAVE_FSL_IMX_PXP),true)
    LOCAL_SHARED_LIBRARIES += \
            libpxp
            LOCAL_CFLAGS += -DHAVE_FSL_IMX_PXP
endif

ifeq ($(PRODUCT_MODEL), SABREAUTO-MX6SX)
    LOCAL_CPPFLAGS += -DVADC_TVIN
endif

ifeq ($(BOARD_SOC_TYPE), IMX7ULP)
    LOCAL_CPPFLAGS += -DIMX7ULP_UVC
endif

LOCAL_CFLAGS += \
        -DANDROID_SDK_VERSION=$(PLATFORM_SDK_VERSION)

LOCAL_CFLAGS += -Wall -Wextra -fvisibility=hidden

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
endif
endif
