#!/bin/sh
# lrzip speed test
# if running as root, uncomment drop_caches line
usage() {
echo "LRZIP Speed Test"
echo "usage: $0 filename"
exit 1
}

[ -z $1 ] && usage


for i in 1 2 3 4 5 6 7 8 9
do
	sync
	sleep 1
#	echo 3 >/proc/sys/vm/drop_caches
#	sleep 1
	lrzip -L$i -S.$i.lrz $1
	[ $? -ne 0 ] && break
done

exit 0

