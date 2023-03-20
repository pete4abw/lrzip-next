#!/bin/sh
# lrzip-next speed test
# if running as root, uncomment drop_caches line
usage() {
echo "LRZIP Speed Test"
echo "usage: $0 filename"
exit 1
}

[ -z $1 ] && usage

export LRZIP=NOCONFIG

# Customize as needed
OUTPUTDIR=$PWD
TESTFILE=testfile.txt
BASENAME=$(basename $1)

echo "Compression/Decompression test for file $1"
echo "Compression/Decompression test for file $1" >$TESTFILE
ls -l $1 1>>$TESTFILE

for i in 1 2 3 4 5 6 7 8 9
do
	for j in bzip2 bzip3 gzip lzo lzma zpaq zstd
	do
		sync
		sleep 1
#		echo 3 >/proc/sys/vm/drop_caches
#		sleep 1
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
