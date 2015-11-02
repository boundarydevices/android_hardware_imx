imx_dirs := libsensors libgps lights wlan libbt-ath3k \
            alsa mx6/libgralloc_wrapper mx6/hwcomposer \
            mx6/power mx6/consumerir mx7/gralloc mx7/hwcomposer \

include $(call all-named-subdir-makefiles,$(imx_dirs))

