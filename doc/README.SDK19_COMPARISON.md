LZMA SDK 19.00 Comparison
=========================

File: linux-5.4.7.tar
File Size: 938,260,480
Test System: 8 core Intel i7-2630QM, 8MB memory

Level|Version|Time|Compression|Dict Size<br>(*Adjusted*)|Note
:-----:|:-------:|----|:-----------:|---------:|----
1|0.631|00:43.76|6.130|65,536|
1|0.702|00:39.53|6.130|65,536
2|0.631|00:45:49|6.159|65,536
2|0.702|00:32.32|6.554|262,144
3|0.631|00:43.14|6.564|262,144
3|0.702|00:42.16|6.753|1,048,576
4|0.631|00:38.69|6.747|1,048,576
4|0.702|00:42.38|6.819|4,194,304
5|0.631|00:43.15|6.733|1,048,576
5|0.702|**01:43.47**|**8.095**|16,777,216|Default 0.7xx
6|0.631|00:41.49|6.798|4,194,304
6|0.702|01:45.80|8.145|*27,262,976*<br>*Adj from 32Mb*
7|0.631|**01:34.11**|**8.044**|16,777,216|Default 0.6xx
7|0.702|01:57:97|8.292|27,262,976<br>*Adj from 32Mb*|Best value
8|0.631|01:26.25|*7.886*|33,554,432|Smaller than level 7!
8|0.702|04:19.18|8.362|*49,283,072*<br>*Adj from 64Mb*
9|0.631|02:26.95|8.308|67,108,864
9|0.702|05:01.69|8.442|*98,566,144*<br>*Adj from 128Mb*

Dictionary and Buffer Sizes
---------------------------

Beginning in lrzip 0.7xx, priority is given to lzma dictionary sizes, and increasing buffer sizes, not maximizing threads. The impact is larger buffer sizes being passed to the backend lzma processor.  Another benefit is that lzma will itself use multiple threads, and the rzip pre-processor still only uses one. Passing lzma larger buffers with larger dictionary sizes give it more data to compress in one pass. However, larger dictionaries and buffers will increase compression time, especially with levels 8 and 9. All these elements are impacted by available ram, CPU speed, and threading.

Comparison of Stream 1 lzma buffer block sizes
----------------------------------------------

Level|lrzip<br>0.631|# Blocks|lrzip<br>0.7xx|# Blocks|Note
:-----:|-----:|:-------:|-----:|:-------:|----
1|104,251,165|9|104,251,165|9
2|104,251,165|9|104,251,165|9
3|104,251,165|8|104,251,165|8
4|104,251,165|8|104,251,165|8
5|104,251,165|7|**134,037,212**|6|Default 0.7xx
6|104,251,165|7|234,565,120|4
7|**54,852,570**|13|234,565,120|3|Default 0.6xx
8|*10,485,760*|64!|469,130,240|2|64 small blocks
9|74,396,558|9|665,303,700|**1**|Entire rzip<br>Buffer

Overhead Computation
--------------------
lzma Memory Overhead and lzma Dictionary size are calculated in the util.c:setup_overhead().
Block sizes and threads used are determined in stream.c:open_stream_out().

Level and Dictionary Size Computation
-------------------------------------
An important difference with SDK 19 is that the lzma compression levels allowed are 1-9, not 1-7 which was carried over since the early days of lrzip. lrzip 0.631 adjusts lzma levels by a factor of 7/9 so the levels passed to the lzma backend are 1-7 even if the user selects level 9 (rzip pre-processing will use 9 levels, however). lrzip 0.7xx does not adjust levels but computes dictionary sizes based on the level selected, not factoring it and passes that level to lzma. The maximum dictionary size in 0.631 is 64MB, and 128MB for 0.7xx. lrzip 0.7xx also allows the user to specify a dictionary size overriding the default levels.

### lrzip 0.7xx Dictionary and Overhead Computation
```
if (LZMA_COMPRESS) {
	if (control->dictSize == 0) {
		switch (control->compression_level) {
		case 1:
		case 2:
		case 3:
		case 4:
		case 5: control->dictSize = (1 << (control->compression_level * 2 + 14));
			break; // 65KB to 16MB
		case 6:
		case 7: control->dictSize = (1 << 25);
			break; // 32MB
		case 8: control->dictSize = (1 << 26);
			break; // 64MB
		case 9: control->dictSize = (1 << 27);
			break; // 128MB -- this is maximum for 32 bits
		default: control->dictSize = (1 << 24);
			break; // 16MB -- should never reach here
		}
	}
	control->overhead = (control->dictSize * 23 / 2) + (6 * 1024 * 1024) + 16384;
}
```

### lrzip 0.631 Dictionary and Overhead Computation
```
if (LZMA_COMPRESS) {
	int level = control->compression_level * 7 / 9;
	if (!level)
		level = 1;
	i64 dictsize = (level <= 5 ? (1 << (level * 2 + 14)) :
			(level == 6 ? (1 << 25) : (1 << 26)));
	control->overhead = (dictsize * 23 / 2) + (6 * 1024 * 1024) + 16384;
}
```
