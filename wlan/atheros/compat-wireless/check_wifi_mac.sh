if [ -e /data/misc/wifi/softmac ]
then
	echo "update softmac"
else
    mkdir /data/misc
	mkdir /data/misc/wifi
	touch /data/misc/wifi/softmac
	echo 02:30:`busybox expr $RANDOM % 89 + 10`:`busybox expr $RANDOM % 89 + 10`:`busybox expr $RANDOM % 89 + 10`:`busybox expr $RANDOM % 89 + 10` > /data/misc/wifi/softmac
	sync
	chmod 775 /data/misc/wifi
	chmod 664 /data/misc/wifi/softmac
fi
