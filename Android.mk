common_imx_dirs := libsensors libgps lights wlan libbt-ath3k
mx5x_dirs := $(common_imx_dirs) mx5x/audio mx5x/libcopybit mx5x/libgralloc  mx5x/hwcomposer mx5x/libcamera mx5x/power
mx6_dirs := $(common_imx_dirs) alsa mx6/libgralloc_wrapper mx6/hwcomposer mx6/power

ifeq ($(TARGET_BOARD_PLATFORM),imx6)
  ifeq ($(IMX_CAMERA_HAL_V2),true)
    mx6_dirs += mx6/libcamera2
  else
    mx6_dirs += mx6/libcamera
  endif
  include $(call all-named-subdir-makefiles,$(mx6_dirs))
else
  ifeq ($(TARGET_BOARD_PLATFORM),imx5x)
    include $(call all-named-subdir-makefiles,$(mx5x_dirs))
  endif
endif
