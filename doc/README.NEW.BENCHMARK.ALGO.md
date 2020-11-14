# A New Way to Compare Results

Compression ratio is, of course, the primary way to judge a
compression program. The smaller the resulting file, the better.

But when time to compress is considered, the best compression may
not be the *best* compression method!

## A way to Rank results

The following table compares different compression methods
against each other in terms of compression **and** in terms of
time to compress. An index is computed for each which describes
as a percent, where each result lies in terms of the all the
others. 100% is the worst and every other method is compared to
it both in terms of resulting **compression** and **time to
compress** expressed as a **percent**. The intent is to compare
one to another and not just rank pure compression or time to
compress. It compares each to the other relatively.

### Maybe Mathematically Shaky!

Then an attempt is made to create an **overall index** and
**Rank** for each method. For this the **Compression Index** and
**Time Index** are ADDED and then divided by 2 to make the
**Overall Index** scale to 100%. The lower the number the better! 

The compression index is computed by comparing the size of a
compressed file to the maximum (worst) size of all methods.
`MYSIZE/MAX(ALLSIZES)` and the time to compress compared to the
maximum (slowest) time to compress `MYTIME/MAX(ALLTIMES)`. The
worst compression ratio will have an index of 100%. The slowest
time to compress will have an index of 100%. All other
compression and time indeces will be relative to the best
compression and slowest time.

#### Example

Compression size: 100   
Worst Compression size: 120   
Compression Index: 100/120 = 83.33% (percent relative to largest
compressed size)

Time to Compress: 60 seconds   
Slowest Time to Compress: 320 seconds   
Time Index: 60/320 = 18.75% (percent relative to the slowest
compression time)

Combine index: (83.33+18.75)/2 = 51.04

This number can be compared to all others in the set.

In this table, you will see comparisons between the following
methods:
* Standalone programs  
  * BZIP2  
  * GZIP  
  * XZ  
  * ZPAQ using Method 3  
  * ZPAQ using Method 4  
* LRZIP Methods  
  * LZMA  
  * BZIP2  
  * GZIP  
  * LZO  
  * RZIP  
  * ZPAQ  

### Highlights of tests

The input file was a tar'ed linux 5.4.74 kernel that was compiled
with the `make defconfig` and `make` in x86_64. So, the input
file had both text (around 940MB) and binary information (around
500MB). The results were interesting.

* LRZIP+LZO was the **fastest** to compress
* LRZIP+ZPAQ had the **best** compression
* XZ was the **slowest** to compress (and had relatively poor
  compression too)
* LRZIP+RZIP had the **worst** compression
* LRZIP+BZIP2 had the **best overall** index score combining
  compression and time to compress
* LRZIP using LZMA (default) fell in the middle of this sample

The differences were small between the top three in compression
but the associated times differed by more than double!

Size|Name|Time|
--: | --- | ---: |
124M | LRZIP_ZPAQ | 4:22
138M | ZPAQ_M4 | 5:00
145M | LRZIP | 2:13

If we Index these results, comparing one to each other:

Size | Name | Time | Comp Index | Time Index | Overall Index | Rank
--: | -- | -- | :--: | :--: | :--: | :--:
145,853,453 | **LRZIP** | 02:13.294 | 100.00% | 44.30% | **72.15%** | 1
123,584,447 | LRZIP_ZPAQ | 04:22.010 | 84.73% | 87.07% |85.90% | 2
137,916,765 | ZPAQ_M4 | 05:00.921 | 94.56% | 100.00% |97.28% | 3

Blending time and compression, LRZIP using LZMA comes out on top
with an overall index score of 72%, vs 86% and 97% for the ZPAQ
variances. Even though it had the worst compression ratio of the
three, it had the best time by far, hence the better overall
score.

## The Test Suite

11 runs of the input file were performed. Before each run all
memory and disk caches were flushed and the disk sync'ed.

Size | Name | Time | Comp Index | Time Index | Overall Index | Rank
--: | -- | -- | :--: | :--: | :--: | :--:
176,191,108 | LRZIP_BZIP2 | 00:28.257 | 19.08% | 6.75% | 12.91% | 1
210,164,154 | LRZIP_GZIP | 00:20.007 | 22.75% | 4.78% | 13.77% | 2
334,907,729 | LRZIP_LZO | **00:13.781** | 36.26% | 3.29% | 19.78% | 3
323,969,716 | GZIP | 00:34.634 | 35.08% | 8.28% | 21.68% | 4
174,376,833 | ZPAQ_M3 | 01:46.530 | 18.88% | 25.45% | 22.17% | 5
**145,853,453** | **LRZIP** | **02:13.294** | **15.79%** | **31.85%** | **23.82%** | **6**
264,445,349 | BZIP2 | 01:32.057 | 28.63% | 22.00% | 25.31% | 7
**123,584,447** | LRZIP_ZPAQ | 04:22.010 | 13.38% | 62.61% | 37.99% | 8
137,916,765 | ZPAQ_M4 | 05:00.921 | 14.93% | 71.90% | 43.42% | 9
*923,624,866* | LRZIP_RZIP | 00:13.493 | 100.00% | 3.22% | 51.61% | 10
215,679,012 | XZ | *06:58.508* | 23.35% | 100.00% | 61.68% | 11
1,451,397,120 | linux-5.4.74.compiled.tar |   |   |   |   |  

### Scores mean nothing

These index scores in and of themselves mean nothing. It's just a
way of comparing different items in an identical way!

Comments are welcome!

November 2020   
Peter Hyman   
pete@peterhyman.com

