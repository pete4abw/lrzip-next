#!/bin/sh
# lrzip-next speed test
# if running as root, uncomment drop_caches line
usage() {
	echo "LRZIP-NEXT Speed Testi\
usage: $0 filename [compression method(s)] [levels]\n\
methods may be one or more of [no-compress, bzip2, bzip3, gzip, lzo, lzma, zpaq, zstd]\n\
for multiple methods, be sure to quote. i.e. \"bzip2 lzo gzip\" etc...\n\
levels may be one or more of [1, 2, 3, 4, 5, 6, 7, 8, 9] in any order.\n\
for levels, must follow methods and levels must be quoted. i.e. \"1 2 3\" etc...\n\
if not methods or levels specified, all methods and levels will be tested.\n\
Output file will be $TESTFILE, a comma-delimited file."
	exit 1
}

die() {
	echo "Error: $1...Aborting"
	exit 1
}

TESTFILE=testfile.csv

[ -z $1 ] && usage
if [ -z "$2" ]; then
	methods="no-compress bzip2 bzip3 gzip lzo lzma zpaq zstd"
else
	methods="$2"
fi
if [ -z "$3" ]; then
	levels="1 2 3 4 5 6 7 8 9"
else
	levels="$3"
fi

export LRZIP=NOCONFIG

# Customize as needed
INPUT=$1
OUTPUTDIR=$PWD
BASENAME=$(basename $INPUT)
UID=$(id -u)
# use binary versions of time and stat
TIME=$(which time)
[ $? -ne 0 ] && die "time program not found" 
STAT=$(which stat)
[ $? -ne 0 ] && die "stat program not found"
INPUTSIZE=$( $STAT --print "%s" $INPUT )
[ $? -ne 0 ] && die "Input file $INPUT not found"
echo "Compression/Decompression test for file $INPUT, $INPUTSIZE  using method(s): $methods"

[ $UID -eq 0 ] && echo "Running as root user."

# Write headers

echo "Method, Level, Input File, Input Size, Compress Time, \
Compressed File, Compressed Size, Decompress Time, Compression Ratio, \
Bits per Byte, Compression MB/s" >$TESTFILE

for LEVEL in $levels
do
	for METHOD in $methods
	do
		sync
		sleep 1
		# root user?
		if [ $UID -eq 0 ]; then
			echo 3 >/proc/sys/vm/drop_caches
			sleep 1
		fi
		OUTPUT=$BASENAME.L$LEVEL.$METHOD.lrz
		echo -n "Testing $INPUT using $METHOD at Level $LEVEL: "
		COMPRESSTIME=$( { $TIME --format "%e" lrzip-next -Qf --$METHOD -L$LEVEL -o $OUTPUTDIR/$OUTPUT $INPUT; } 2>&1 )
		[ $? -ne 0 ] && die "An error occured during compression!"
		OUTPUTSIZE=$( $STAT --print "%s" $OUTPUT )
		echo "Compression Time: $COMPRESSTIME, Compressed Size: $OUTPUTSIZE"
		echo -n "Decompressing $OUTPUT: "
		DECOMPRESSTIME=$( { $TIME --format "%e" lrzip-next -Qt $OUTPUTDIR/$OUTPUT; } 2>&1 )
		[ $? -ne 0 ] && die "An error occured during decompression!"
		echo "Decompression Time: $DECOMPRESSTIME"
		COMPRESSIONRATIO=$(echo "scale=3; $INPUTSIZE/$OUTPUTSIZE" | bc -l)
		BITSPERBYTE=$(echo "scale=3; 8*$OUTPUTSIZE/$INPUTSIZE" | bc -l)
		COMPRESSIONMBS=$(echo "scale=3; $INPUTSIZE/(1048576*$COMPRESSTIME)" | bc -l)
		echo "$METHOD, $LEVEL, $INPUT, $INPUTSIZE, $COMPRESSTIME, $OUTPUT, $OUTPUTSIZE, $DECOMPRESSTIME, $COMPRESSIONRATIO, $BITSPERBYTE, $COMPRESSIONMBS" >> $TESTFILE
	done
done
echo "Results stored in: $TESTFILE"
exit 0
