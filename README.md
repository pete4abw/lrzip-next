lrzip-next - Long Range ZIP
======================

v 0.7.99

## This is a development branch. Not for production use!

## NEW: LZMA SDK 21.02, ZPAQ 7.15, SCRYPT Bitcoin style key derivation

Many new changes, not in the main branch, including:
* latest LZMA and ZPAQ libraries (**ABI Compatible** with earlier versions. No need to re-compress files).
* more helpful usage messages
* filters for x86 and other processors, and delta
* ASM DeCompressor (up to 40% faster. x86_64 only)
* ability to set rzip pre-compression level independently of compression level
* variable compressibility threshold testing
* ability to set specific LZMA Dictionary Size
* improved info reporting
* improved memory management favoring large chunk and dictionary sizes over maxmizing number of threads
* improved ZPAQ processing by analyzing data prior to sending to compressor
* many bug fixes including validating a file prior to decompression (prevents corrupt file decompression before it starts!)
* use of git describe to present current version without changing configure.ac
* lz4 Threshold testing (replaces lzo)
* File info `lrzip-next -i` will now fetch info from encrypted files
* `lrzip-next -i` will not print percent info when file size is not known
* Substitute libgcrypt functions for separate sources for **md5** and **sha512** hash functions, and **aes 128 bit** encryption.  
* SCRYPT Key Derivation.

(This will allow for future bug fixes and possibly using different encryption methods through a standard library.)

(See original README for more historical info)

### Download and Build
`$ git clone -b lzma-sdk-21.02 https://github.com/pete4abw/lrzip-next`\
or if you desire to also download the **lrzip-fe** front end\
`$ git clone -b lzma-sdk-21.02 --recurse-submodules https://github.com/pete4abw/lrzip-next`

If you forget use --recurse-submodules and want to download lrzip-fe separately, use these commands:
```
$ git clone -b lzma-sdk-21.02 https://github.com/pete4abw/lrzip-next
$ cd lrzip-next
$ git submodule update --init (to download lrzip-fe)
```
**NEW**! Tarballs will now compile from version 0.7.44 onward. gitdesc.sh has been made
more intelligent! Recommend downloading from master, not from past tags/releases.

**NEW**! If you just want to try `lrzip-next`, download the static binaries. No compilation
necessary. [Visit this link to download](https://peterhyman.com/download/lrzip-next/).

Verify file with gnupg key 0x467FBF7D.

### Build
```
cd lrzip-next
$ ./autogen.sh
$ ./configure [options] (see configure --help for all options)
```
If any required libraries or compilers are missing, **configure** will report and stop.
```
$ make [-j#] (for parallel make where # is typically number of processors)
$ make install (as root)
```

### How it Works
**lrzip-next** applies a two-step process (optionally three-step process if filters are used)
and reads file or STDIN input, passes it to the **rzip** pre-processor (and optional filter).
The rzip pre-processor applies long-range redundancy reduction and then passes the streams of
data to a back-end compressor. **lrzip--next** will, by default, test each stream with a *compressibility*
test using **lz4** prior to compression. The selected back-end compressor works on smaller data
sets and ignore streams of data that may not compress well. The end result is significantly
faster compression than standalone compressors and much faster decompression.

**lrzip-next**'s compressors are:
* lzma (default)
* gzip
* bzip2
* lzo
* zpaq
* rzip (pre-processed only)

**lrzip-next**'s memory management scheme permits maximum use of system ram to pre-process files and then compress them.

### Usage and Integration
**lrzip-next** operates on one file at a time. It's default mode is to create a compressed file
using lzma compression at level 7. To operate on more than one file, the included **lrztar**
application can be run or **lrzip** can be inserted into a **tar** command.

`lrzip-next [-options] file` will compress `file` using lzma compression at level 7.\
`tar --use-compress-program='lrzip-next [-options]' [-tar options] -cf file.tar.lrz file...` will compress file(s) or
directory(ies) using lzma compression at level 7.\
`tar [-tar options] -cf- file... | lrzip-next [-options] -o file.tar.lrz` will pipe tar output to **lrzip-next** with default options.

The differences between the three usage examples are memory available for compression/decompression.\
The first and third options will use roughly 1/3 of total ram for compression/decompression.\
The second option willl use roughly 1/6 of total ram for compression/decompression.\
All depends if the **lrzip-next** options **-m** or **-w** are used. Normally lrzip-next will determine optimum memory usage.

Using `tar` with `lrzip-next` has advantages since it does not create a separate (possibly large) tar file, but rather passes the
tar'red information right to `lrzip-next` which then compresses it on-the-fly. Similarly, on decompression, no intermediate
tar file has to be created. `lrzip-next` output is piped right to `tar` which then can extract files/directories as specified.

For a list of all options and their usage, see the manpage or just type `lrzip-next`
with no options. The highlevel compression options are listed here.

Option|Meaning
---|---
**Compression Options**
--lzma|lzma compression (default)
-b, --bzip2|bzip2 compression
-g, --gzip|gzip compression using zlib
-l, --lzo|lzo compression (ultra fast)
-n, --no-compress|no backend compression - prepare for other compressor
-z, --zpaq|zpaq compression (best, extreme compression, extremely slow)
-L, --level level|set lzma/bzip2/gzip compression level (1-9, default 7)
--dictsize|Set lzma Dictionary Size for LZMA ds=0 to 40 expressed as 2<<11, 3<<11, 2<<12, 3<<12...2<<31-1
**Filtering Options**
--x86|Use x86 filter (for all compression modes)
--arm|Use ARM filter (for all compression modes)
--armt|Use ARMT filter (for all compression modes)
--ppc|Use PPC filter (for all compression modes)
--sparc|Use SPARC filter (for all compression modes)
--ia64|Use IA64 filter (for all compression modes)
--delta [1..32]|Use Delta filter (for all compression modes) (1 (default) -17, then multiples of 16 to 256)
**General Options**
-h, -?, --help|show help
-H, --hash|display md5 hash integrity information
-i, --info|show compressed file information
-q, --quiet|don't show compression progress
-p, --threads value|Set processor count to override number of threads
-r, --recursive|operate recursively on directories
-v[v], --verbose|Increase verbosity
-V, --version|display software version and license

#### Backends:
rzip:
<http://rzip.samba.org/>\
lzo:
<http://www.oberhumer.com/opensource/lzo/>\
lzma:
<http://www.7-zip.org/>\
gzip:
<https://www.gnu.org/software/gzip/>\
bzip2:
<https://sourceware.org/bzip2/>\
zpaq:
<http://mattmahoney.net/dc/>

### Thanks (CONTRIBUTORS)
|Person(s)|Thanks for|
|---|---|
|`Andrew Tridgell`|`rzip`|
|`Markus Oberhumer`|`lzo`|
|`Igor Pavlov`|`lzma`|
|`Jean-Loup Gailly & Mark Adler`|`zlib`|
|`Matt Mahoney`|`zpaq` integration code|
|***`Con Kolivas`***|***Original Code, binding all of this together, managing the project, original `README`***|
|`Christian Leber`|`lzma` compatibility layer|
|`Michael J Cohen`|Darwin/OSX support|
|`Lasse Collin`|fixes to `LZMALib.cpp` and `Makefile.in`|
|Everyone else who coded along the way (add yourself where appropriate if that's you)|Miscellaneous Coding|
|***`Peter Hyman`***|***Most of the `0.19` to `0.24` changes and all post 0.631 changes***|
|`^^^^^^^^^^^`|Updating the multithreaded `lzma` lib
|`^^^^^^^^^^^`|SDK Updates
|`^^^^^^^^^^^`|All sorts of other features
|`^^^^^^^^^^^`|x86_64 Assembler work
|`^^^^^^^^^^^`|Major updates after 0.631
|`^^^^^^^^^^^`|Pre-compression filtering
|`^^^^^^^^^^^`|Documentation and Benchmarking
|`René Rhéaume`|Fixing executable stacks|
|`Ed Avis`|Various fixes|
|`Jukka Laurila`|Additional Darwin/OSX support|
|`George Makrydakis`|`lrztar` wrapper|
|`Ulrich Drepper`|*special* implementation of md5|
|**`Michael Blumenkrantz`**|New config tools|
|`^^^^^^^^^^^^^^^^^^^^`|`liblrzip`|
|Authors of `PolarSSL`|Encryption code|
|`Serge Belyshev`|Extensive help, advice, and patches to implement secure encryption|
|`Jari Aalto`|Fixing typos, esp. in code|
|`Carlo Alberto Ferraris`|Code cleanup
|`Haneef Mubarak`|Cleanup, Rewrite, and GH Markdown of `README` --> `README.md`|

Persons above are listed in chronological order of first contribution to **lrzip**.
Person(s) with names in **bold** have multiple major contributions.
Person(s) with names in *italics* have made massive contributions
Person(s) with names in ***both*** have made innumerable massive contributions.

#### README Authors

For **lrzip-next**\
Peter Hyman (pete4abw on Guthub) <pete@peterhyman.com>\
Sun, 04 Jan 2009, README\
Mon, 28 Apr 2021: README.md

For **lrzip**\
Con Kolivas (ckolivas` on GitHub) <kernel@kolivas.org>\
Fri, 10 June 2016: README

Mostly Rewritten + GFMified:\
Haneef Mubarak (haneefmubarak on GitHub)\
Sun/Mon Sep 01-02 2013: README.md (now OLDREADME.md)
