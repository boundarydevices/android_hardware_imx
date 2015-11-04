imx_dirs := libsensors libgps lights wlan libbt-ath3k \
            alsa libsensors_sensorhub mx6/libgralloc_wrapper \
            mx6/hwcomposer mx6/power mx6/libcamera mx6/libcamera2 \
            mx7/gralloc mx7/hwcomposer \

include $(call all-named-subdir-makefiles,$(imx_dirs))

