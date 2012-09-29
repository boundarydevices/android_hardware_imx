if [ -e /device/wifi/softmac ]
then
	echo "update softmac"
else
	mount -w -o remount /device
	mkdir /device/wifi
	touch /device/wifi/softmac
	echo 02:30:`busybox expr $RANDOM % 89 + 10`:`busybox expr $RANDOM % 89 + 10`:`busybox expr $RANDOM % 89 + 10`:`busybox expr $RANDOM % 89 + 10` > /device/wifi/softmac
	sync
	chmod 775 /vendor/wifi
	chmod 664 /vendor/wifi/softmac
	mount -r -o remount /device
fi
