lrzip-next - Long Range ZIP
======================

## LZMA SDK 22.01 (March 2023)

Tag v0.10.4

See **FEATURES** file for list of lrzip-next enhancements.

(See original README for more historical info)

### Download and Build

`$ git clone https://github.com/pete4abw/lrzip-next`\
or if you desire to also download the **lrzip-fe** front end\
`$ git clone --recurse-submodules https://github.com/pete4abw/lrzip-next`

If you forget use --recurse-submodules and want to download lrzip-fe separately, use these commands:
```
$ git clone https://github.com/pete4abw/lrzip-next
$ cd lrzip-next
$ git submodule update --init (to download lrzip-fe)
```
**NEW**! Tarballs will now compile from version 0.7.44 onward. gitdesc.sh has been made
more intelligent! Recommend downloading from master, not from past tags/releases.

**NEW**! If you just want to try `lrzip-next`, download the static binaries. No compilation
necessary. See [Releases](https://github.com/pete4abw/lrzip-next/releases).

Verify file with gnupg key 0xEB2C5812.

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
* bzip3
* lzo
* zpaq
* zstd
* rzip (pre-processed only)

**lrzip-next**'s memory management scheme permits maximum use of system ram to pre-process files and then compress them.

### Usage and Integration
See Discussions and Wiki for info.

### Thanks
Con Kolivas - the creator of `lrzip`\
Others listed in original README file.

#### README Authors

For **lrzip-next**\
Peter Hyman (pete4abw on Guthub) <pete@peterhyman.com>\
Sun, 04 Jan 2009, README\
Mon, 28 Apr 2021: README.md

For **lrzip**\
Con Kolivas (ckolivas` on GitHub) <kernel@kolivas.org>\
Fri, 10 June 2016: README
