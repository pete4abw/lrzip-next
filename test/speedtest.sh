#!/bin/sh
# lrzip-next speed test
# if running as root, uncomment drop_caches line
usage() {
echo "LRZIP-NEXT Speed Test"
echo "usage: $0 filename [compression method(s)]"
echo "for multiple methods, be sure to quote. i.e. \"bzip2 lzo gzip\" etc..."
exit 1
}

[ -z $1 ] && usage
if [ -z $2 ]; then
	methods="bzip2 bzip3 gzip lzo lzma zpaq zstd"
else
	methods="$2"
fi

export LRZIP=NOCONFIG

# Customize as needed
OUTPUTDIR=$PWD
TESTFILE=testfile.txt
BASENAME=$(basename $1)
UID=$(id -u)
echo "Compression/Decompression test for file $1 using method(s): $methods"
echo "Compression/Decompression test for file $1 using method(s): $methods" >$TESTFILE
ls -l $1 1>>$TESTFILE

[ $UID -eq 0 ] && echo "Running as root user."

for i in 1 2 3 4 5 6 7 8 9
do
	for j in $methods
	do
		sync
		sleep 1
		# root user?
		if [ $UID -eq 0 ]; then
			echo 3 >/proc/sys/vm/drop_caches
			sleep 1
		fi
		OUTPUT=$BASENAME.L$i.$j.lrz
		echo "Testing $j at Level $i"
		echo -n "Testing $j at Level $i : " >>$TESTFILE
		lrzip-next -qf --$j -L$i -o $OUTPUTDIR/$OUTPUT $1 1>>$TESTFILE
		if [ $? -ne 0 ] ; then
			echo "An error occured during compression!"
			exit 1
		fi
		echo "Decompressing $OUTPUT"
		echo -n "Decompressing $OUTPUT : " >>$TESTFILE
		lrzip-next -qt $OUTPUTDIR/$OUTPUT 1>>$TESTFILE
		if [ $? -ne 0 ] ; then
			echo "An error occured during decompression!"
			exit 1
		fi
	done
done

echo "Test completed. Here are the files..." >>$TESTFILE
(cd $OUTPUTDIR
ls -l $BASENAME*.lrz 1>>$TESTFILE
)
exit 0
