LOCAL_PATH:= $(call my-dir)

ifeq ($(BOARD_HAVE_IMX_EVS),true)
##################################
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    service.cpp \
    EvsEnumerator.cpp \
    EvsV4lCamera.cpp \
    EvsGlDisplay.cpp \
    GlWrapper.cpp \
    VideoCapture.cpp \
    bufferCopy.cpp \
    bufferConvert.cpp


LOCAL_SHARED_LIBRARIES := \
    android.hardware.automotive.evs@1.0 \
    libui \
    libgui \
    libEGL \
    libGLESv2 \
    libbase \
    libbinder \
    libcutils \
    libhardware \
    libhidlbase \
    libhidltransport \
    liblog \
    libutils \
    libion \
    libopencl-2d

LOCAL_C_INCLUDES +=  \
    $(IMX_PATH)/imx/display/display \
    vendor/nxp-opensource/imx/opencl-2d

LOCAL_WHOLE_STATIC_LIBRARIES := libionallocator
LOCAL_MODULE := android.hardware.automotive.evs@1.0-EvsEnumeratorHw

LOCAL_VENDOR_MODULE := true
LOCAL_MODULE_TAGS := optional
LOCAL_STRIP_MODULE := keep_symbols

LOCAL_CFLAGS += -DLOG_TAG=\"EvsSampleDriver\"
LOCAL_CFLAGS += -DGL_GLEXT_PROTOTYPES -DEGL_EGLEXT_PROTOTYPES
LOCAL_CFLAGS += -Wall -Werror -Wunused -Wunreachable-code

# NOTE:  It can be helpful, while debugging, to disable optimizations
#LOCAL_CFLAGS += -O0 -g

include $(BUILD_EXECUTABLE)
endif
