common_imx_dirs := libsensors alsa libgps
mx5x_dirs := $(common_imx_dirs) mx5x/libcopybit mx5x/libgralloc  mx5x/hwcomposer mx5x/libcamera
mx6_dirs := $(common_imx_dirs) mx6/libgralloc_wrapper mx6/hwcomposer mx6/libcamera

ifeq ($(TARGET_BOARD_PLATFORM),imx6)
  include $(call all-named-subdir-makefiles,$(mx6_dirs))
else
  ifeq ($(TARGET_BOARD_PLATFORM),imx5x)
    include $(call all-named-subdir-makefiles,$(mx5x_dirs))
  endif
endif
