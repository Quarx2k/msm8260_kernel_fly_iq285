adb shell rmmod dmb
adb push dmb.ko /data
adb shell insmod /data/dmb.ko