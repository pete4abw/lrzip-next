#!/bin/sh
# lrzip-next speed test
# if running as root, uncomment drop_caches line
usage() {
	echo "LRZIP-NEXT Speed Test\n\
usage: $0 -f filename [ -m MEHOD(S) ] [ -l LEVELS ] [ -o TESTFILE ] [ -x EXTRA OPTIONS ] [ -p PROGRAM ][ -h | -? ]\n\n\
METHODS may be one or more of [no-compress, bzip2, bzip3, gzip, lzo, lzma, zpaq, zstd]\n\
- for multiple METHODS, be sure to quote. i.e. \"bzip2 lzo gzip\" etc...\n\
- if no METHODS are selected, all compression methods will be used.\n\
LEVELS may be one or more of [1, 2, 3, 4, 5, 6, 7, 8, 9] in any order.\n\
- for multiple LEVELS, be sure to quote, i,e, \"1 2 3\"\n\
- if no LEVELS are selected, then all compression levels will be used.\n\
if TESTFILE is not speciied, default is testfile.csv\n\
- Output file will be a comma-delimited (CSV) file.\n\
Extra Options will be passed to lrzip-next, e.g. \"--x86\" or \"-p1\", etc. Be sure to quote.\n\
- Extra options will only be applied to Compression, not Decompression test.\n
- Program defaults to lrzip-next. If a different lrzip program is desired to be tested,\n
use tge `-p` option with the name of the binary. Note. Only works with lrzip-next and related\n
binaries."
	exit 1
}

die() {
	echo "Error: $1...Aborting"
	exit 1
}

while getopts "f:m:l:o:x:p:h?" Options
do
	case ${Options} in
		f)	INPUT=${OPTARG} ;;
		m)	METHODS="${OPTARG}" ;;
		l)	LEVELS="${OPTARG}" ;;
		o)	TESTFILE="${OPTARG}" ;;
		x)	EXTRAOPTS="${OPTARG}" ;;
		p)	PROGRAM=${OPTARG} ;;
		h|?|*)	usage ;;
	esac
done

[ $# -eq "0" ] && usage
[ -z "$INPUT" ] && die "No Input File to test"
[ -z "$METHODS" ] && METHODS="no-compress bzip2 bzip3 gzip lzo lzma zpaq zstd"
[ -z "$LEVELS" ] && LEVELS="1 2 3 4 5 6 7 8 9"
[ -z "$TESTFILE" ] && TESTFILE="testfile.csv"
[ -z "$PROGRAM" ] && PROGRAM="lrzip-next"

[ ! $(which $PROGRAM) ] && die "$PROGRAM not found"

export LRZIP=NOCONFIG

# Customize as needed
OUTPUTDIR=$PWD
BASENAME=$(basename $INPUT)
UID=$(id -u)
# use binary versions of time and stat
TIME=time
STAT=$(which stat)
[ $? -ne 0 ] && die "stat program not found"
INPUTSIZE=$( $STAT --print "%s" $INPUT )
[ $? -ne 0 ] && die "Input file $INPUT not found"
[ $UID -eq 0 ] && echo "Running as root user."

echo -n "Compression/Decompression test for file $INPUT, $INPUTSIZE using method(s): $METHODS with level(s) $LEVELS"
[ ! -z "$EXTRAOPTS" ] && echo -n " using user-selected options $EXTRAOPTS"
echo

# Write headers

echo "Method, Level, Input File, Compressed File, Input Size, Compressed Size, \
Compressed Time, Decompress Time, Compression Ratio, Bits per Byte, \
Compression MB/s, Decompression MB/s" >$TESTFILE

for LEVEL in $LEVELS
do
	for METHOD in $METHODS
	do
		sync
		# root user?
		if [ $UID -eq 0 ]; then
			echo 3 >/proc/sys/vm/drop_caches
		fi
		OUTPUT=$BASENAME.L$LEVEL.$METHOD.lrz
		echo -n "Testing $INPUT using:level $LEVEL, method $METHOD"
		[ ! -z "$EXTRAOPTS" ] && echo -n ", extra options $EXTRAOPTS"
		echo -n ": "
		COMPRESSTIME=$( { $TIME --format "%e" $PROGRAM -Qf --$METHOD -L$LEVEL -o $OUTPUTDIR/$OUTPUT $EXTRAOPTS $INPUT; } 2>&1 )
		[ $? -ne 0 ] && die "An error occured during compression!"
		OUTPUTSIZE=$( $STAT --print "%s" $OUTPUT )
		echo "Compression Time: $COMPRESSTIME, Compressed Size: $OUTPUTSIZE"
		echo -n "Decompressing $OUTPUT: "
		sync
		DECOMPRESSTIME=$( { $TIME --format "%e" $PROGRAM -Qt $OUTPUTDIR/$OUTPUT; } 2>&1 )
		[ $? -ne 0 ] && die "An error occured during decompression!"
		echo "Decompression Time: $DECOMPRESSTIME"
		COMPRESSIONRATIO=$(echo "scale=3; $INPUTSIZE/$OUTPUTSIZE" | bc -l)
		BITSPERBYTE=$(echo "scale=3; 8*$OUTPUTSIZE/$INPUTSIZE" | bc -l)
		COMPRESSIONMBS=$(echo "scale=3; $INPUTSIZE/(1048576*$COMPRESSTIME)" | bc -l)
		DECOMPRESSIONMBS=$(echo "scale=3; $OUTPUTSIZE/(1048576*$DECOMPRESSTIME)" | bc -l)
		echo "$METHOD, $LEVEL, $INPUT, $OUTPUT, $INPUTSIZE, $OUTPUTSIZE, $COMPRESSTIME, $DECOMPRESSTIME, $COMPRESSIONRATIO, $BITSPERBYTE, $COMPRESSIONMBS, $DECOMPRESSIONMBS" >> $TESTFILE
	done
done
echo "Results stored in: $TESTFILE"
exit 0
