/*
   Copyright (C) 2006-2016,2018 Con Kolivas
   Copyright (C) 2011, 2020 Peter Hyman
   Copyright (C) 1998-2003 Andrew Tridgell

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/types.h>
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#include <fcntl.h>
#include <sys/statvfs.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <arpa/inet.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <sys/mman.h>
#include <sys/time.h>
#include <termios.h>
#ifdef HAVE_ENDIAN_H
# include <endian.h>
#elif HAVE_SYS_ENDIAN_H
# include <sys/endian.h>
#endif
#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif
#include <math.h>
#include <utime.h>

#include "rzip.h"
#include "runzip.h"
#include "util.h"
#include "stream.h"

#include "LzmaDec.h"		// decode for get_fileinfo

#include <gcrypt.h>		// for rng

#define MAGIC_LEN	(18)	// new v 0.8 magic header
#define OLD_MAGIC_LEN	(24)	// Just to read older versions
#define MAGIC_HEADER	(6)	// to validate file initially

static void release_hashes(rzip_control *control);

static i64 fdout_seekto(rzip_control *control, i64 pos)
{
	if (TMP_OUTBUF) {
		pos -= control->out_relofs;
		control->out_ofs = pos;
		if (unlikely(pos > control->out_len || pos < 0)) {
			print_err("Trying to seek to %'"PRId64" outside tmp outbuf in fdout_seekto\n", pos);
			return -1;
		}

		return 0;
	}
	return lseek(control->fd_out, pos, SEEK_SET);
}

i64 get_ram(rzip_control *control)
{
#ifdef __APPLE__
# include <sys/sysctl.h>
	int mib[2];
	size_t len;
	i64 ramsize;

	mib[0] = CTL_HW;
	mib[1] = HW_MEMSIZE;
	sysctl(mib, 2, &ramsize, &len, NULL, 0);
#else /* __APPLE__ */
	i64 ramsize;
	FILE *meminfo;
	char aux[256];

	ramsize = (i64)sysconf(_SC_PHYS_PAGES) * PAGE_SIZE;
	if (ramsize <= 0) {
		/* Workaround for uclibc which doesn't properly support sysconf */
		if(!(meminfo = fopen("/proc/meminfo", "r")))
			fatal_return(("Failed to open /proc/meminfo\n"), -1);

		while(!feof(meminfo) && !fscanf(meminfo, "MemTotal: %'"PRId64" kB", &ramsize)) {
			if (unlikely(fgets(aux, sizeof(aux), meminfo) == NULL)) {
				fclose(meminfo);
				fatal_return(("Failed to fgets in get_ram\n"), -1);
			}
		}
		if (fclose(meminfo) == -1)
			fatal_return(("Failed to close /proc/meminfo"), -1);
		ramsize *= 1024;
	}
#endif
	if (ramsize <= 0)
		fatal_return(("No memory or can't determine ram? Can't continue.\n"), -1);
	return ramsize;
}

i64 nloops(i64 seconds, uchar *b1, uchar *b2)
{
	i64 nloops;
	int nbits;

	nloops = ARBITRARY_AT_EPOCH * pow(MOORE_TIMES_PER_SECOND, seconds);
	if (nloops < ARBITRARY)
		nloops = ARBITRARY;
	for (nbits = 0; nloops > 255; nbits ++)
		nloops = nloops >> 1;
	*b1 = nbits;
	*b2 = nloops;
	return nloops << nbits;
}


bool write_magic(rzip_control *control)
{
	unsigned char magic[MAGIC_LEN] = {
		'L', 'R', 'Z', 'I', LRZIP_MAJOR_VERSION, LRZIP_MINOR_VERSION
	};

	 /* In encrypted files, the size is left unknown
	 * and instead the salt is stored here to preserve space. */
	// FIXME. I think we can do better. 8 bytes is no reason to save space
	if (ENCRYPT)
		memcpy(&magic[6], &control->salt, 8);
	else if (control->eof) {
		i64 esize = htole64(control->st_size);	// we know file size even when piped

		memcpy(&magic[6], &esize, 8);
	}
	/* This is a flag that the archive contains an md5 sum at the end
	 * which can be used as an integrity check instead of crc check.
	 * crc is still stored for compatibility with 0.5 versions.
	 */
	if (!NO_MD5)
		magic[14] = 1;
	if (ENCRYPT)
		magic[15] = 1;

	magic[16] = 0;
	if (FILTER_USED) {
		// high order 5 bits for delta offset - 1, 0-255. Low order 3 bits for filter type 1-7
		// offset bytes will be incremented on decompression
		if (control->delta > 1)					// only need to do if offset > 1
			magic[16] = ((control->delta <= 17 ? ((control->delta-1) << 3) :
				((control->delta / 16) + 16 -1) << 3));	// delta offset-1, if applicable
		magic[16] += control->filter_flag;			// filter flag
	}

	/* save LZMA dictionary size */
	if (LZMA_COMPRESS)
		magic[17] = lzma2_prop_from_dic(control->dictSize);
	else if (ZPAQ_COMPRESS) {
		/* Save zpaq compression level and block size as one byte */
		/* High order bits = 128 + (16 * Compression Level 3-5)
		 * Low order bits = Block Size 1-11
		 * 128 necessary to distinguish in decoding LZMA which is 1-40
		 * 1CCC BBBB in binary */
		magic[17] = 0b10000000 + (control->zpaq_level << 4) + control->zpaq_bs;
		/* Decoding would be
		 * magic byte & 127 (clear high bit)
		 * zpaq_bs = magic byte & 0X0F
		 * zpaq_level = magic_byte >> 4
		 */
	}

	if (unlikely(fdout_seekto(control, 0)))
		fatal_return(("Failed to seek to BOF to write Magic Header\n"), false);

	if (unlikely(put_fdout(control, magic, MAGIC_LEN) != MAGIC_LEN))
		fatal_return(("Failed to write magic header\n"), false);
	control->magic_written = 1;
	return true;
}

static inline i64 enc_loops(uchar b1, uchar b2)
{
	return (i64)b2 << (i64)b1;
}

// retriev lzma properties

static void get_lzma_prop(rzip_control *control, unsigned char *magic)
{
	int i;
	/* restore LZMA compression flags only if stored */
	for (i = 0; i < 5; i++)
		control->lzma_properties[i] = *magic++;

	return;
}

static void get_md5(rzip_control *control, unsigned char *magic)
{
	/* Whether this archive contains md5 data at the end or not */
	if (*magic == 1)
		control->flags |= FLAG_MD5;
	else
		print_verbose("Unknown hash, falling back to CRC\n");

	return;
}

// get encrypted salt

static void get_encryption(rzip_control *control, unsigned char *magic, unsigned char *salt)
{
	if (*magic) {
		if (*magic == 1)
			control->flags |= FLAG_ENCRYPT;
		else
			failure(("Unknown encryption\n"));
		/* In encrypted files, the size field is used to store the salt
		 * instead and the size is unknown, just like a STDOUT chunked
		 * file */
		memcpy(&control->salt, salt, 8);
		control->st_size = 0;
		control->encloops = enc_loops(control->salt[0], control->salt[1]);
		print_maxverbose("Encryption hash loops %'"PRId64"\n", control->encloops);
	} else if (ENCRYPT) {
		print_output("Asked to decrypt a non-encrypted archive. Bypassing decryption.\n");
		control->flags &= ~FLAG_ENCRYPT;
	}

	return;
}

// expected size

static void get_expected_size(rzip_control *control, unsigned char *magic)
{
	i64 expected_size;

	memcpy(&expected_size, &magic[6], 8);
	control->st_size = le64toh(expected_size);

	return;
}

// filter

static void get_filter(rzip_control *control, unsigned char *magic)
{
	int i;
	// any value means filter used
	if (*magic) {
		control->filter_flag = *magic & FILTER_MASK;			// Filter flag
		if (control->filter_flag == FILTER_FLAG_DELTA) {		// Get Delta Offset if needed
			i = (*magic & DELTA_OFFSET_MASK) >> 3;		// delta offset stored as value-1
			control->delta = (i <= 16 ? i + 1 : (i-16 + 1) * 16);	// need to restore actual value+1
		}
	}

	return;
}

// retrieve magic for lrzip-next <v 0.4
static void get_magic_lt4(rzip_control *control, unsigned char *magic)
{
	uint32_t v;
	i64 expected_size;

	/* Support the convoluted way we described size in versions < 0.40 */
	memcpy(&v, &magic[6], 4);
	expected_size = ntohl(v);
	memcpy(&v, &magic[10], 4);
	control->st_size |= ((i64)ntohl(v)) << 32;

	if (magic[16])
		get_lzma_prop(control, &magic[16]);

	return;
}

// retrieve magic for lrzip <v 0.6
static void get_magic_lt6(rzip_control *control, unsigned char *magic)
{
	if (!magic[22])	// not encrypted
		get_expected_size(control, magic);

	if (magic[16])	// lzma
		get_lzma_prop(control, &magic[16]);

	get_md5(control, &magic[21]);

	return;
}

// retrieve magic for lrzip v6

static void get_magic_lt7(rzip_control *control, unsigned char *magic)
{
	// only difference is encryption

	get_magic_lt6(control, magic);
	get_encryption(control, &magic[22], &magic[6]);

	return;
}

// retrieve magic for lrzip-next v7

static void get_magic_lt8(rzip_control *control, unsigned char *magic)
{
	int i;

	if (!magic[23])	// not encrypted
		get_expected_size(control, magic);
	get_encryption(control, &magic[23], &magic[6]);

	// get filter
	if (magic[16])
		get_filter(control, &magic[16]);

	if (magic[17])	// lzma
		get_lzma_prop(control, &magic[17]);

	get_md5(control, &magic[22]);

	return;
}

// new lrzip-next v8 magic header format.

static void get_magic_v8(rzip_control *control, unsigned char *magic)
{
	int i;

	if (!magic[15])	// not encrypted
		get_expected_size(control, magic);
	get_encryption(control, &magic[15], &magic[6]);

	// get filter
	if (magic[16])
		get_filter(control, &magic[16]);

	if (magic[17] > 0 && magic[17] <= 40)	// lzma dictionary
	{
		control->dictSize = LZMA2_DIC_SIZE_FROM_PROP(magic[17]);	// decode dictionary
		control->lzma_properties[0] = LZMA_LC_LP_PB;			// constant for lc, lp, pb 0x5D
		/* from LzmaDec.c */
		for (i = 0; i < 4; i++)						// lzma2 to lzma dictionary expansion
			control->lzma_properties[1 + i] = (Byte)(control->dictSize >> (8 * i));
	}
	else if (magic[17] & 0b10000000)	// zpaq block and compression level stored
	{
		control->zpaq_bs = magic[17] & 0b00001111;	// low order bits are block size
		magic[17] &= 0b01110000;			// strip high bit
		control->zpaq_level = magic[17] >> 4;		// divide by 16
	}

	get_md5(control, &magic[14]);

	return;
}

static bool get_magic(rzip_control *control, unsigned char *magic)
{
	memcpy(&control->major_version, &magic[4], 1);
	memcpy(&control->minor_version, &magic[5], 1);

	if (control->major_version == 0) {
		if (control->minor_version < 4)
			get_magic_lt4(control, magic);
		else if (control->minor_version < 6)
			get_magic_lt6(control, magic);
		else if (control->minor_version < 7)
			get_magic_lt7(control, magic);
		else if (control->minor_version < 8)
			get_magic_lt8(control, magic);
		else
			get_magic_v8(control, magic);
	}

	if (control->major_version > LRZIP_MAJOR_VERSION ||
	    (control->major_version == LRZIP_MAJOR_VERSION && control->minor_version > LRZIP_MINOR_VERSION))
		print_output("Attempting to work with file produced by newer lrzip version %'d.%'d file.\n", control->major_version, control->minor_version);

	if (control->major_version == 0 && control->minor_version < 6)
		control->eof = 1;

	return true;
}

static bool read_magic(rzip_control *control, int fd_in, i64 *expected_size)
{
	unsigned char magic[OLD_MAGIC_LEN];	// Make at least big enough for old magic

	memset(magic, 0, sizeof(magic));
	/* Initially read only file type and version */
	if (unlikely(read(fd_in, magic, MAGIC_HEADER) != MAGIC_HEADER))
		fatal_return(("Failed to read initial magic header\n"), false);

	if (unlikely(strncmp(magic, "LRZI", 4)))
		failure_return(("Not an lrzip file\n"), false);

	if (magic[4] == 0 && magic[5] < 8)	// read rest of header
	{
		if (unlikely(read(fd_in, &magic[6], OLD_MAGIC_LEN - MAGIC_HEADER) != OLD_MAGIC_LEN - MAGIC_HEADER))
			fatal_return(("Failed to read magic header\n"), false);
	} else if (magic[4] == 0 && magic[5] == 8) { // 0.8 file
		if (unlikely(read(fd_in, &magic[6], MAGIC_LEN - MAGIC_HEADER) != MAGIC_LEN - MAGIC_HEADER))
			fatal_return(("Failed to read magic header\n"), false);
	}

	if (unlikely(!get_magic(control, magic)))
		return false;
	*expected_size = control->st_size;
	return true;
}

/* show lrzip-next version
 * helps preserve output format when validating
 */
static void show_version(rzip_control *control)
{
	print_verbose("Detected lrzip version %'d.%'d file.\n", control->major_version, control->minor_version);
}

/* preserve ownership and permissions where possible */
static bool preserve_perms(rzip_control *control, int fd_in, int fd_out)
{
	struct stat st;

	if (unlikely(fstat(fd_in, &st)))
		fatal_return(("Failed to fstat input file\n"), false);
	if (unlikely(fchmod(fd_out, (st.st_mode & 0666))))
		print_verbose("Warning, unable to set permissions on %s\n", control->outfile);

	/* chown fail is not fatal_return(( */
	if (unlikely(fchown(fd_out, st.st_uid, st.st_gid)))
		print_verbose("Warning, unable to set owner on %s\n", control->outfile);
	return true;
}

static bool preserve_times(rzip_control *control, int fd_in)
{
	struct utimbuf times;
	struct stat st;

	if (unlikely(fstat(fd_in, &st)))
		fatal_return(("Failed to fstat input file\n"), false);
	times.actime = 0;
	times.modtime = st.st_mtime;
	if (unlikely(utime(control->outfile, &times)))
		print_verbose("Warning, unable to set time on %s\n", control->outfile);

	return true;
}

/* Open a temporary outputfile to emulate stdout */
int open_tmpoutfile(rzip_control *control)
{
	int fd_out;

	if (STDOUT && !TEST_ONLY)
		print_verbose("Outputting to stdout.\n");
	if (control->tmpdir) {
		control->outfile = realloc(NULL, strlen(control->tmpdir) + 16);
		if (unlikely(!control->outfile))
			fatal_return(("Failed to allocate outfile name\n"), -1);
		strcpy(control->outfile, control->tmpdir);
		strcat(control->outfile, "lrzipout.XXXXXX");
	}

	fd_out = mkstemp(control->outfile);
	if (fd_out == -1) {
		print_progress("WARNING: Failed to create out tmpfile: %s, will fail if cannot perform %scompression entirely in ram\n",
			       control->outfile, DECOMPRESS ? "de" : "");
	} else
		register_outfile(control, control->outfile, TEST_ONLY || STDOUT || !KEEP_BROKEN);
	return fd_out;
}

static bool fwrite_stdout(rzip_control *control, void *buf, i64 len)
{
	uchar *offset_buf = buf;
	ssize_t ret, nmemb;
	i64 total;

	total = 0;
	while (len > 0) {
		nmemb = MIN(len, one_g);
		ret = fwrite(offset_buf, 1, nmemb, control->outFILE);
		if (unlikely(ret != nmemb))
			fatal_return(("Failed to fwrite %'"PRId32" bytes in fwrite_stdout\n", nmemb), false);
		len -= ret;
		offset_buf += ret;
		total += ret;
	}
	fflush(control->outFILE);
	return true;
}

bool write_fdout(rzip_control *control, void *buf, i64 len)
{
	uchar *offset_buf = buf;
	ssize_t ret, nmemb;

	while (len > 0) {
		nmemb = MIN(len, one_g);
		ret = write(control->fd_out, offset_buf, (size_t)nmemb);
		if (unlikely(ret != nmemb))
			fatal_return(("Failed to write %'"PRId32" bytes to fd_out in write_fdout\n", nmemb), false);
		len -= ret;
		offset_buf += ret;
	}
	return true;
}

bool flush_tmpoutbuf(rzip_control *control)
{
	if (!TEST_ONLY) {
		print_maxverbose("Dumping buffer to physical file.\n");
		if (STDOUT) {
			if (unlikely(!fwrite_stdout(control, control->tmp_outbuf, control->out_len)))
				return false;
		} else {
			if (unlikely(!write_fdout(control, control->tmp_outbuf, control->out_len)))
				return false;
		}
	}
	control->out_relofs += control->out_len;
	control->out_ofs = control->out_len = 0;
	return true;
}

/* Dump temporary outputfile to perform stdout */
bool dump_tmpoutfile(rzip_control *control, int fd_out)
{
	FILE *tmpoutfp;
	int tmpchar;

	if (unlikely(fd_out == -1))
		fatal_return(("Failed: No temporary outfile created, unable to do in ram\n"), false);
	/* flush anything not yet in the temporary file */
	fsync(fd_out);
	tmpoutfp = fdopen(fd_out, "r");
	if (unlikely(tmpoutfp == NULL))
		fatal_return(("Failed to fdopen out tmpfile\n"), false);
	rewind(tmpoutfp);

	if (!TEST_ONLY) {
		print_verbose("Dumping temporary file to control->outFILE.\n");
		while ((tmpchar = fgetc(tmpoutfp)) != EOF)
			putchar(tmpchar);
		fflush(control->outFILE);
		rewind(tmpoutfp);
	}

	if (unlikely(ftruncate(fd_out, 0)))
		fatal_return(("Failed to ftruncate fd_out in dump_tmpoutfile\n"), false);
	return true;
}

/* Used if we're unable to read STDIN into the temporary buffer, shunts data
 * to temporary file */
bool write_fdin(rzip_control *control)
{
	uchar *offset_buf = control->tmp_inbuf;
	i64 len = control->in_len;
	ssize_t ret;

	while (len > 0) {
		ret = MIN(len, one_g);
		ret = write(control->fd_in, offset_buf, (size_t)ret);
		if (unlikely(ret <= 0))
			fatal_return(("Failed to write to fd_in in write_fdin\n"), false);
		len -= ret;
		offset_buf += ret;
	}
	return true;
}

/* Open a temporary inputfile to perform stdin decompression */
int open_tmpinfile(rzip_control *control)
{
	int fd_in = -1;

	/* Use temporary directory if there is one */
	if (control->tmpdir) {
		control->infile = malloc(strlen(control->tmpdir) + 15);
		if (unlikely(!control->infile))
			fatal_return(("Failed to allocate infile name\n"), -1);
		strcpy(control->infile, control->tmpdir);
		strcat(control->infile, "lrzipin.XXXXXX");
		fd_in = mkstemp(control->infile);
	}

	/* Try the current directory */
	if (fd_in == -1) {
		dealloc(control->infile);
		control->infile = malloc(16);
		if (unlikely(!control->infile))
			fatal_return(("Failed to allocate infile name\n"), -1);
		strcpy(control->infile, "lrzipin.XXXXXX");
		fd_in = mkstemp(control->infile);
	}

	/* Use /tmp if nothing is writeable so far */
	if (fd_in == -1) {
		dealloc(control->infile);
		control->infile = malloc(20);
		if (unlikely(!control->infile))
			fatal_return(("Failed to allocate infile name\n"), -1);
		strcpy(control->infile, "/tmp/lrzipin.XXXXXX");
		fd_in = mkstemp(control->infile);
	}

	if (fd_in == -1) {
		print_progress("WARNING: Failed to create in tmpfile: %s, will fail if cannot perform %scompression entirely in ram\n",
			       control->infile, DECOMPRESS ? "de" : "");
	} else {
		register_infile(control, control->infile, (DECOMPRESS || TEST_ONLY) && STDIN);
		/* Unlink temporary file immediately to minimise chance of files left
		* lying around in cases of failure_return((. */
		if (unlikely(unlink(control->infile))) {
			fatal("Failed to unlink tmpfile: %s\n", control->infile);
			close(fd_in);
			return -1;
		}
	}
	return fd_in;
}

static bool read_tmpinmagic(rzip_control *control)
{
	/* just in case < 0.8 file */
	char magic[OLD_MAGIC_LEN];
	int i, tmpchar;

	memset(magic, 0, sizeof(magic));
	/* first read in MAGIC_LEN bytes. 0.8+ */
	for (i = 0; i < MAGIC_LEN; i++) {
		tmpchar = getchar();
		if (unlikely(tmpchar == EOF))
			failure_return(("Reached end of file on STDIN prematurely on v05 magic read\n"), false);
		magic[i] = (char)tmpchar;
	}
	/* if < 0.8 read in last bytes */
	if (magic[4] == 0 && magic[5] < 8) {
		for ( ; i < OLD_MAGIC_LEN; i++) {
			tmpchar = getchar();
			if (unlikely(tmpchar == EOF))
				failure_return(("Reached end of file on STDIN prematurely on v05 magic read\n"), false);
			magic[i] = (char)tmpchar;
		}
	}

	return get_magic(control, magic);
}

/* Read data from stdin into temporary inputfile */
bool read_tmpinfile(rzip_control *control, int fd_in)
{
	FILE *tmpinfp;
	int tmpchar;

	if (fd_in == -1)
		return false;
	if (control->flags & FLAG_SHOW_PROGRESS)
		fprintf(control->msgout, "Copying from stdin.\n");
	tmpinfp = fdopen(fd_in, "w+");
	if (unlikely(tmpinfp == NULL))
		fatal_return(("Failed to fdopen in tmpfile\n"), false);

	while ((tmpchar = getchar()) != EOF)
		fputc(tmpchar, tmpinfp);

	fflush(tmpinfp);
	rewind(tmpinfp);
	return true;
}

/* To perform STDOUT, we allocate a proportion of ram that is then used as
 * a pseudo-temporary file */
static bool open_tmpoutbuf(rzip_control *control)
{
	i64 maxlen = control->maxram;
	void *buf;

	while (42) {
		round_to_page(&maxlen);
		buf = malloc(maxlen);
		if (buf) {
			print_maxverbose("Malloced %'"PRId64" for tmp_outbuf\n", maxlen);
			break;
		}
		maxlen = maxlen / 3 * 2;
		if (maxlen < 100000000)
			fatal_return(("Unable to even malloc 100MB for tmp_outbuf\n"), false);
	}
	control->flags |= FLAG_TMP_OUTBUF;
	/* Allocate slightly more so we can cope when the buffer overflows and
	 * fall back to a real temporary file */
	control->out_maxlen = maxlen + control->page_size;
	control->tmp_outbuf = buf;
	if (!DECOMPRESS && !TEST_ONLY)
		control->out_ofs = control->out_len = MAGIC_LEN;
	return true;
}

/* We've decided to use a temporary output file instead of trying to store
 * all the output buffer in ram so we can free up the ram and increase the
 * maximum sizes of ram we can allocate */
void close_tmpoutbuf(rzip_control *control)
{
	control->flags &= ~FLAG_TMP_OUTBUF;
	dealloc(control->tmp_outbuf);
	if (!BITS32)
		control->usable_ram = control->maxram += control->ramsize / 18;
}

static bool open_tmpinbuf(rzip_control *control)
{
	control->flags |= FLAG_TMP_INBUF;
	control->in_maxlen = control->maxram;
	control->tmp_inbuf = malloc(control->maxram + control->page_size);
	if (unlikely(!control->tmp_inbuf))
		fatal_return(("Failed to malloc tmp_inbuf in open_tmpinbuf\n"), false);
	return true;
}

void clear_tmpinbuf(rzip_control *control)
{
	control->in_len = control->in_ofs = 0;
}

bool clear_tmpinfile(rzip_control *control)
{
	if (unlikely(lseek(control->fd_in, 0, SEEK_SET)))
		fatal_return(("Failed to lseek on fd_in in clear_tmpinfile\n"), false);
	if (unlikely(ftruncate(control->fd_in, 0)))
		fatal_return(("Failed to truncate fd_in in clear_tmpinfile\n"), false);
	return true;
}

/* As per temporary output file but for input file */
void close_tmpinbuf(rzip_control *control)
{
	control->flags &= ~FLAG_TMP_INBUF;
	dealloc(control->tmp_inbuf);
	if (!BITS32)
		control->usable_ram = control->maxram += control->ramsize / 18;
}

static int get_pass(rzip_control *control, char *s)
{
	int len;

	memset(s, 0, PASS_LEN - SALT_LEN);
	if (control->passphrase)
		strncpy(s, control->passphrase, PASS_LEN - SALT_LEN - 1);
	else if (unlikely(fgets(s, PASS_LEN - SALT_LEN, stdin) == NULL))
		failure_return(("Failed to retrieve passphrase\n"), -1);
	len = strlen(s);
	if (len > 0 && ('\r' ==  s[len - 1] || '\n' == s[len - 1]))
		s[len - 1] = '\0';
	if (len > 1 && ('\r' ==  s[len - 2] || '\n' == s[len - 2]))
		s[len - 2] = '\0';
	len = strlen(s);
	if (unlikely(0 == len))
		failure_return(("Empty passphrase\n"), -1);
	return len;
}

static bool get_hash(rzip_control *control, int make_hash)
{
	char *passphrase, *testphrase;
	struct termios termios_p;
	int prompt = control->passphrase == NULL;

	passphrase = calloc(PASS_LEN, 1);
	testphrase = calloc(PASS_LEN, 1);
	control->salt_pass = calloc(PASS_LEN, 1);
	control->hash = calloc(HASH_LEN, 1);
	if (unlikely(!passphrase || !testphrase || !control->salt_pass || !control->hash)) {
		fatal("Failed to calloc encrypt buffers in compress_file\n");
		dealloc(testphrase);
		dealloc(passphrase);
		return false;
	}
	mlock(passphrase, PASS_LEN);
	mlock(testphrase, PASS_LEN);
	mlock(control->salt_pass, PASS_LEN);
	mlock(control->hash, HASH_LEN);

	/* lrzip library callback code removed */
	/* Disable stdin echo to screen */
	tcgetattr(fileno(stdin), &termios_p);
	termios_p.c_lflag &= ~ECHO;
	tcsetattr(fileno(stdin), 0, &termios_p);
retry_pass:
	if (prompt)
		print_output("Enter passphrase: ");
	control->salt_pass_len = get_pass(control, passphrase) + SALT_LEN;
	if (prompt)
		print_output("\n");
	if (make_hash) {
		if (prompt)
			print_output("Re-enter passphrase: ");
		get_pass(control, testphrase);
		if (prompt)
			print_output("\n");
		if (strcmp(passphrase, testphrase)) {
			print_output("Passwords do not match. Try again.\n");
			goto retry_pass;
		}
	}
	termios_p.c_lflag |= ECHO;
	tcsetattr(fileno(stdin), 0, &termios_p);
	memset(testphrase, 0, PASS_LEN);
	memcpy(control->salt_pass, control->salt, SALT_LEN);
	memcpy(control->salt_pass + SALT_LEN, passphrase, PASS_LEN - SALT_LEN);
	lrz_stretch(control);
	memset(passphrase, 0, PASS_LEN);
	munlock(passphrase, PASS_LEN);
	munlock(testphrase, PASS_LEN);
	dealloc(testphrase);
	dealloc(passphrase);
	return true;
}

static void release_hashes(rzip_control *control)
{
	memset(control->salt_pass, 0, PASS_LEN);
	memset(control->hash, 0, HASH_LEN);
	munlock(control->salt_pass, PASS_LEN);
	munlock(control->hash, HASH_LEN);
	dealloc(control->salt_pass);
	dealloc(control->hash);
}

bool get_header_info(rzip_control *control, int fd_in, uchar *ctype, i64 *c_len,
		     i64 *u_len, i64 *last_head, int chunk_bytes)
{
	uchar enc_head[25 + SALT_LEN];
	if (ENCRYPT) {
		// read in salt
		// first 8 bytes, instead of chunk bytes and size
		if (unlikely(read(fd_in, enc_head, SALT_LEN) != SALT_LEN))
			fatal_return(("Failed to read encrypted header in get_header_info\n"), false);
	}
	if (unlikely(read(fd_in, ctype, 1) != 1))
		fatal_return(("Failed to read in get_header_info\n"), false);

	*c_len = *u_len = *last_head = 0;
	if (control->major_version == 0) {
		if (control->minor_version < 4) {
			u32 c_len32, u_len32, last_head32;

			if (unlikely(read(fd_in, &c_len32, 4) != 4))
				fatal_return(("Failed to read in get_header_info"), false);
			if (unlikely(read(fd_in, &u_len32, 4) != 4))
				fatal_return(("Failed to read in get_header_info"), false);
			if (unlikely(read(fd_in, &last_head32, 4) != 4))
				fatal_return(("Failed to read in get_header_info"), false);
			c_len32 = le32toh(c_len32);
			u_len32 = le32toh(u_len32);
			last_head32 = le32toh(last_head32);
			*c_len = c_len32;
			*u_len = u_len32;
			*last_head = last_head32;
		} else {
			// header the same after v 0.4 except for chunk bytes
			int read_len;

			if (control->minor_version == 5)
				read_len = 8;
			else
				read_len = chunk_bytes;
			if (unlikely(read(fd_in, c_len, read_len) != read_len))
				fatal_return(("Failed to read in get_header_info"), false);
			if (unlikely(read(fd_in, u_len, read_len) != read_len))
				fatal_return(("Failed to read in get_header_info"), false);
			if (unlikely(read(fd_in, last_head, read_len) != read_len))
				fatal_return(("Failed to read_i64 in get_header_info"), false);
			*c_len = le64toh(*c_len);
			*u_len = le64toh(*u_len);
			*last_head = le64toh(*last_head);
			if (ENCRYPT) {
				// decrypt header suppressing printing max verbose message
				if (unlikely(!decrypt_header(control, enc_head, ctype, c_len, u_len, last_head, LRZ_VALIDATE)))
					fatal_return(("Failed to decrypt header in get_header_info\n"), false);
			}
		}
	}	// control->major_version
	return true;
}

static double percentage(i64 num, i64 den)
{
	double d_num, d_den;

	if (den < 100) {
		d_num = num * 100;
		d_den = den;
		if (!d_den)
			d_den = 1;
	} else {
		d_num = num;
		d_den = den / 100;
	}
	return d_num / d_den;
}

// If Decompressing or Testing, omit printing, just read file and see if valid
// using construct if (INFO)
// Encrypted files cannot be checked now
bool get_fileinfo(rzip_control *control)
{
	i64 u_len, c_len, second_last, last_head, utotal = 0, ctotal = 0, ofs, stream_head[2];
	i64 expected_size, infile_size, chunk_size = 0, chunk_total = 0;
	int header_length, stream = 0, chunk = 0;
	char *tmp, *infilecopy = NULL;
	char chunk_byte = 0;
	long double cratio;
	uchar ctype = 0;
	uchar save_ctype = 255;
	struct stat st;
	int fd_in;
	CLzmaProps p; // decode lzma header
	int lzma_ret;

	// Take out all STDIN checks
	struct stat fdin_stat;

	if (unlikely(stat(control->infile, &fdin_stat)))
		fatal_return(("File %s not found...\n", control->infile), false);
	else if (unlikely(!S_ISREG(fdin_stat.st_mode)))
		fatal_return(("File %s us not a regular file. lrzip-next cannot continue...\n", control->infile), false);
	else
		infilecopy = strdupa(control->infile);

	fd_in = open(infilecopy, O_RDONLY);
	if (unlikely(fd_in == -1))
		fatal_return(("Failed to open %s\n", infilecopy), false);

	/* Get file size */
	if (unlikely(fstat(fd_in, &st)))
		fatal_goto(("bad magic file descriptor!?\n"), error);
	infile_size = st.st_size;

	/* Get decompressed size */
	if (unlikely(!read_magic(control, fd_in, &expected_size)))
		goto error;

	if (INFO) show_version(control);		// show version if not validating

	if (ENCRYPT && !control->salt_pass_len)		// Only get password if needed
		if (unlikely(!get_hash(control, 0)))
			return false;

	if (control->major_version == 0) {
		if (control->minor_version > 4) {
			if (unlikely(read(fd_in, &chunk_byte, 1) != 1))
				fatal_goto(("Failed to read chunk_byte in get_fileinfo\n"), error);
			if (unlikely(chunk_byte < 1 || chunk_byte > 8))
				failure_goto(("Invalid chunk bytes %'d\n", chunk_byte), error);
			if (control->minor_version > 5) {
				if (unlikely(read(fd_in, &control->eof, 1) != 1))
					fatal_goto(("Failed to read eof in get_fileinfo\n"), error);
				if (!ENCRYPT) {
					if (unlikely(read(fd_in, &chunk_size, chunk_byte) != chunk_byte))
						fatal_goto(("Failed to read chunk_size in get_fileinfo\n"), error);
					chunk_size = le64toh(chunk_size);
					if (unlikely(chunk_size < 0))
						failure_goto(("Invalid chunk size %'"PRId64"\n", chunk_size), error);
				} else {
					chunk_byte=8;
					chunk_size=0;
				}
			}
		}
	}

	/* determine proper offset for chunk headers */
	if (control->major_version == 0) {
		if (ENCRYPT) {
			// Only for version 6, 7, and 8
			header_length=33; // 25 + 8
			// salt is first 8 bytes
			ofs = control->minor_version < 8 ? 26 : 20;
		} else if (control->minor_version < 4) {
			ofs = 24;
			header_length = 13;
		} else if (control->minor_version == 4) {
			ofs = 24;
			header_length = 25;
		} else if (control->minor_version == 5) {
			ofs = 25;
			header_length = 25;
		} else if (control->minor_version < 8) {
			ofs = 26 + chunk_byte;
			header_length = 1 + (chunk_byte * 3);
		} else if (control->minor_version == 8) {
			ofs = 20 + chunk_byte;
			header_length = 1 + (chunk_byte * 3);
		}
	}

	if (control->major_version == 0 && control->minor_version < 6 &&
		!expected_size)
			goto done;
next_chunk:
	stream = 0;
	stream_head[0] = 0;
	stream_head[1] = stream_head[0] + header_length;

	if (!ENCRYPT) {
		chunk_total += chunk_size;
		if (unlikely(chunk_byte && (chunk_byte > 8 || chunk_size <= 0)))
			failure_goto(("Invalid chunk data\n"), error);
	}

	if (INFO) {
		print_verbose("Rzip chunk:       %'d\n", ++chunk);
		print_verbose("Chunk byte width: %'d\n", chunk_byte);
		print_verbose("Chunk size:       ");
		if (!ENCRYPT)
			print_verbose("%'"PRId64"\n", chunk_size);
		else
			print_verbose("N/A Encrypted File\n");
	}
	while (stream < NUM_STREAMS) {
		int block = 1;

		second_last = 0;
		if (unlikely(lseek(fd_in, stream_head[stream] + ofs, SEEK_SET) == -1))
			fatal_goto(("Failed to seek to header data in get_fileinfo\n"), error);

		if (unlikely(!get_header_info(control, fd_in, &ctype, &c_len, &u_len, &last_head, chunk_byte)))
			return false;

		if (ENCRYPT && ctype != CTYPE_NONE)
			failure_goto(("Invalid stream ctype (%02x) for encrypted file. Bad Password?\n", ctype), error);

		if (INFO) {
			print_verbose("Stream: %'d\n", stream);
			print_maxverbose("Offset: %'"PRId64"\n", stream_head[stream] + ofs);
			print_verbose("%s\t%s\t%s\t%16s / %14s", "Block","Comp","Percent","Comp Size", "UComp Size");
			print_maxverbose("%18s : %14s", "Offset", "Head");
			print_verbose("\n");
		}
		do {
			i64 head_off;

			if (unlikely(last_head && last_head < second_last))
				failure_goto(("Invalid earlier last_head position, corrupt archive.\n"), error);
			second_last = last_head;
			if (!ENCRYPT) {
				if (unlikely(last_head + ofs > infile_size))
					failure_goto(("Offset greater than archive size, likely corrupted/truncated archive.\n"), error);
			} else {
				if (unlikely(last_head + ofs + header_length > infile_size))
					failure_goto(("Offset greater than archive size, likely corrupted/truncated archive.\n"), error);
			}

			if (unlikely((head_off = lseek(fd_in, last_head + ofs, SEEK_SET)) == -1))
				fatal_goto(("Failed to seek to header data in get_fileinfo\n"), error);
			if (unlikely(!get_header_info(control, fd_in, &ctype, &c_len, &u_len,
					&last_head, chunk_byte)))
				return false;
			if (unlikely(last_head < 0 || c_len < 0 || u_len < 0))
				failure_goto(("Entry negative, likely corrupted archive.\n"), error);
			if (INFO) print_verbose("%'d\t", block);
			if (ctype == CTYPE_NONE) {
				if (INFO) print_verbose("none");
			} else if (ctype == CTYPE_BZIP2) {
				if (INFO) print_verbose("bzip2");
			} else if (ctype == CTYPE_LZO) {
				if (INFO) print_verbose("lzo");
			} else if (ctype == CTYPE_LZMA) {
				if (INFO) print_verbose("lzma");
			} else if (ctype == CTYPE_GZIP) {
				if (INFO) print_verbose("gzip");
			} else if (ctype == CTYPE_ZPAQ) {
				if (INFO) print_verbose("zpaq");
			} else
				failure_goto(("Unknown Compression Type: %'d\n", ctype), error);
			if (save_ctype == 255)
				save_ctype = ctype; /* need this for lzma when some chunks could have no compression
						     * and info will show rzip + none on info display if last chunk
						     * is not compressed. Adjust for all types in case it's used in
						     * the future */
			utotal += u_len;
			ctotal += c_len;
			if (INFO) {
				print_verbose("\t%5.1f%%\t%'16"PRId64" / %'14"PRId64"", percentage(c_len, u_len), c_len, u_len);
				print_maxverbose("%'18"PRId64" : %'14"PRId64"", head_off, last_head);
				print_verbose("\n");
			}
			block++;
		} while (last_head);
		++stream;
	}

	if (unlikely((ofs = lseek(fd_in, c_len, SEEK_CUR)) == -1))
		fatal_goto(("Failed to lseek c_len in get_fileinfo\n"), error);

	if (ofs >= infile_size - (HAS_MD5 ? MD5_DIGEST_SIZE : 0))
		goto done;
	else if (ENCRYPT)
		if (ofs+header_length > infile_size)
			goto done;

	/* Chunk byte entry */
	if (control->major_version == 0) {
		if(control->minor_version > 4 && !ENCRYPT) {
			if (unlikely(read(fd_in, &chunk_byte, 1) != 1))
				fatal_goto(("Failed to read chunk_byte in get_fileinfo\n"), error);
			if (unlikely(chunk_byte < 1 || chunk_byte > 8))
				failure_goto(("Invalid chunk bytes %'d\n", chunk_byte), error);
			ofs++;
			if (control->minor_version > 5) {
				if (unlikely(read(fd_in, &control->eof, 1) != 1))
					fatal_goto(("Failed to read eof in get_fileinfo\n"), error);
				if (unlikely(read(fd_in, &chunk_size, chunk_byte) != chunk_byte))
					fatal_goto(("Failed to read chunk_size in get_fileinfo\n"), error);
				chunk_size = le64toh(chunk_size);
				if (unlikely(chunk_size < 0))
					failure_goto(("Invalid chunk size %'"PRId64"\n", chunk_size), error);
				ofs += 1 + chunk_byte;
				header_length = 1 + (chunk_byte * 3);
			}
		}
		else if (ENCRYPT) {
			// ENCRYPTED
			// no change to chunk_byte
			ofs+=10;
			// no change to header_length
		}
	}

	goto next_chunk;
done:
	cratio = (long double)expected_size / (long double)infile_size;
	if (unlikely(ofs > infile_size))
		failure_goto(("Offset greater than archive size, likely corrupted/truncated archive.\n"), error);

	if (INFO) {
		print_output("\nSummary\n=======\n");
		print_output("File: %s\nlrzip-next version: %'d.%'d %sfile\n\n", infilecopy,
				control->major_version, control->minor_version, ENCRYPT ? "Encrypted " : "");

		if (!expected_size)
			print_output("Due using %s, expected decompression size not available\n",
				ENCRYPT ? "Encryption": "Compression to STDOUT");
		print_verbose("  Stats         Percent       Compressed /   Uncompressed\n  -------------------------------------------------------\n");
		/* If we can't show expected size, tailor output for it */
		if (expected_size) {
			print_verbose("  Rzip:         %5.1f%%\t%'16"PRId64" / %'14"PRId64"\n",
					percentage (utotal, expected_size),
					utotal, expected_size);
			print_verbose("  Back end:     %5.1f%%\t%'16"PRId64" / %'14"PRId64"\n",
					percentage(ctotal, utotal),
					ctotal, utotal);
			print_verbose("  Overall:      %5.1f%%\t%'16"PRId64" / %'14"PRId64"\n",
					percentage(ctotal, expected_size),
					ctotal, expected_size);
		} else {
			print_verbose("  Rzip:         Unavailable\n");
			print_verbose("  Back end:     %5.1f%%\t%'16"PRId64" / %'14"PRId64"\n", percentage(ctotal, utotal), ctotal, utotal);
			print_verbose("  Overall:      Unavailable\n");
		}

		print_output("\n  Compression Method: ");
		if (save_ctype == CTYPE_NONE)
			print_output("rzip alone\n");
		else if (save_ctype == CTYPE_BZIP2)
			print_output("rzip + bzip2\n");
		else if (save_ctype == CTYPE_LZO)
			print_output("rzip + lzo\n");
		else if (save_ctype == CTYPE_LZMA) {
			print_output("rzip + lzma -- ");
			if (lzma_ret=LzmaProps_Decode(&p, control->lzma_properties, sizeof(control->lzma_properties))==SZ_OK)
				print_output("lc = %'d, lp = %'d, pb = %'d, Dictionary Size = %'d\n", p.lc, p.lp, p.pb, p.dicSize);
			else
				print_err("Corrupt LZMA Properties\n");
		}
		else if (save_ctype == CTYPE_GZIP)
			print_output("rzip + gzip\n");
		else if (save_ctype == CTYPE_ZPAQ) {
			print_output("rzip + zpaq ");
			if (control->zpaq_level)	// update magic with zpaq coding.
				print_output("-- Compression Level = %d, Block Size = %d, %'dMB\n", control->zpaq_level, control->zpaq_bs,
						(1 << control->zpaq_bs));
			else	// early 0.8 or <0.8 file without zpaq coding in magic header
				print_output("\n");
		}
		else
			print_output("Dunno wtf\n");

		/* show filter used */
		if (FILTER_USED) {
			print_output("  Filter Used: %s",
					((control->filter_flag == FILTER_FLAG_X86) ? "x86" :
					((control->filter_flag == FILTER_FLAG_ARM) ? "ARM" :
					((control->filter_flag == FILTER_FLAG_ARMT) ? "ARMT" :
					((control->filter_flag == FILTER_FLAG_PPC) ? "PPC" :
					((control->filter_flag == FILTER_FLAG_SPARC) ? "SPARC" :
					((control->filter_flag == FILTER_FLAG_IA64) ? "IA64" :
					((control->filter_flag == FILTER_FLAG_DELTA) ? "Delta" : "wtf?"))))))));
			if (control->filter_flag == FILTER_FLAG_DELTA)
				print_output(", offset - %'d", control->delta);
			print_output("\n");
		}
		print_output("\n");

		if (expected_size) {
			print_output("  Decompressed file size: %'14"PRIu64"\n", expected_size);
			print_output("  Compressed file size:   %'14"PRIu64"\n", infile_size);
			print_output("  Compression ratio:      %14.3Lfx\n", cratio);
		} else {
			print_output("  Decompressed file size:    Unavailable\n");
			print_output("  Compressed file size:   %'14"PRIu64"\n", infile_size);
			print_output("  Compression ratio:         Unavailable\n");
		}
	} /* end if (INFO) */

	if (HAS_MD5) {
		unsigned char md5_stored[MD5_DIGEST_SIZE];
		int i;

		if (INFO) {
			if (unlikely(lseek(fd_in, -MD5_DIGEST_SIZE, SEEK_END) == -1))
				fatal_return(("Failed to seek to md5 data in get_fileinfo.\n"), false);
			if (unlikely(read(fd_in, md5_stored, MD5_DIGEST_SIZE) != MD5_DIGEST_SIZE))
				fatal_return(("Failed to read md5 data in get_fileinfo.\n"), false);
			if (ENCRYPT)
				if (unlikely(!lrz_decrypt(control, md5_stored, MD5_DIGEST_SIZE, control->salt_pass, LRZ_VALIDATE)))
					fatal_return(("Failure decrypting MD5 in get_fileinfo.\n"), false);
			print_output("\n  MD5 Checksum: ");
			for (i = 0; i < MD5_DIGEST_SIZE; i++)
				print_output("%02x", md5_stored[i]);
			print_output("\n");
		}
	} else {
		if (INFO) print_output("\n  CRC32 used for integrity testing\n");
	}

out:
	if (unlikely(close(fd_in)))
		fatal_return(("Failed to close fd_in in get_fileinfo\n"), false);
	return true;
error:
	dealloc(control->outfile);
	return false;
}

/*
  compress one file from the command line
*/
bool compress_file(rzip_control *control)
{
	const char *tmp, *tmpinfile; 	/* we're just using this as a proxy for control->infile.
					 * Spares a compiler warning
					 */
	int fd_in = -1, fd_out = -1;
	char header[MAGIC_LEN];

	control->flags |= FLAG_MD5;	/* MD5 now the default */
	if (ENCRYPT)
		if (unlikely(!get_hash(control, 1)))
			return false;
	memset(header, 0, sizeof(header));

	if (!STDIN) {
		 /* is extension at end of infile? */
		if ((tmp = strrchr(control->infile, '.')) && !strcmp(tmp, control->suffix)) {
			print_err("%s: already has %s suffix. Skipping...\n", control->infile, control->suffix);
			return false;
		}

		fd_in = open(control->infile, O_RDONLY);
		if (unlikely(fd_in == -1))
			fatal_return(("Failed to open %s\n", control->infile), false);
	} else
		fd_in = fileno(control->inFILE);

	if (!STDOUT) {
		if (control->outname) {
				/* check if outname has control->suffix */
				if (*(control->suffix) == '\0') /* suffix is empty string */
					control->outfile = strdup(control->outname);
				else if ((tmp=strrchr(control->outname, '.')) && strcmp(tmp, control->suffix)) {
					control->outfile = malloc(strlen(control->outname) + strlen(control->suffix) + 1);
					if (unlikely(!control->outfile))
						fatal_goto(("Failed to allocate outfile name\n"), error);
					strcpy(control->outfile, control->outname);
					strcat(control->outfile, control->suffix);
					print_output("Suffix added to %s.\nFull pathname is: %s\n", control->outname, control->outfile);
				} else	/* no, already has suffix */
					control->outfile = strdup(control->outname);
		} else {
			/* default output name from control->infile
			 * test if outdir specified. If so, strip path from filename of
			 * control->infile
			*/
			if (control->outdir && (tmp = strrchr(control->infile, '/')))
				tmpinfile = tmp + 1;
			else
				tmpinfile = control->infile;

			control->outfile = malloc((control->outdir == NULL? 0: strlen(control->outdir)) + strlen(tmpinfile) + strlen(control->suffix) + 1);
			if (unlikely(!control->outfile))
				fatal_goto(("Failed to allocate outfile name\n"), error);

			if (control->outdir) {	/* prepend control->outdir */
				strcpy(control->outfile, control->outdir);
				strcat(control->outfile, tmpinfile);
			} else
				strcpy(control->outfile, tmpinfile);
			strcat(control->outfile, control->suffix);
			print_output("Output filename is: %s\n", control->outfile);
		}

		fd_out = open(control->outfile, O_RDWR | O_CREAT | O_EXCL, 0666);
		if (FORCE_REPLACE && (-1 == fd_out) && (EEXIST == errno)) {
			if (unlikely(unlink(control->outfile)))
				fatal_goto(("Failed to unlink an existing file: %s\n", control->outfile), error);
			fd_out = open(control->outfile, O_RDWR | O_CREAT | O_EXCL, 0666);
		}
		if (unlikely(fd_out == -1)) {
			/* We must ensure we don't delete a file that already
			 * exists just because we tried to create a new one */
			control->flags |= FLAG_KEEP_BROKEN;
			fatal_goto(("Failed to create %s\n", control->outfile), error);
		}
		control->fd_out = fd_out;
		if (!STDIN) {
			if (unlikely(!preserve_perms(control, fd_in, fd_out)))
				goto error;
		}
	} else {
		if (unlikely(!open_tmpoutbuf(control)))
			goto error;
	}

	/* Write zeroes to header at beginning of file */
	if (unlikely(!STDOUT && write(fd_out, header, sizeof(header)) != sizeof(header)))
		fatal_goto(("Cannot write file header\n"), error);

	rzip_fd(control, fd_in, fd_out);

	/* Wwrite magic at end b/c lzma does not tell us properties until it is done */
	if (!STDOUT) {
		if (unlikely(!write_magic(control)))
			goto error;
	}

	if (ENCRYPT)
		release_hashes(control);

	if (unlikely(!STDIN && !STDOUT && !preserve_times(control, fd_in))) {
		fatal("Failed to preserve times on output file\n");
		goto error;
	}

	if (unlikely(close(fd_in))) {
		fatal("Failed to close fd_in\n");
		fd_in = -1;
		goto error;
	}
	if (unlikely(!STDOUT && close(fd_out)))
		fatal_return(("Failed to close fd_out\n"), false);
	if (TMP_OUTBUF)
		close_tmpoutbuf(control);

	if (!KEEP_FILES && !STDIN) {
		if (unlikely(unlink(control->infile)))
			fatal_return(("Failed to unlink %s\n", control->infile), false);
	}

	dealloc(control->outfile);
	return true;
error:
	if (!STDIN && (fd_in > 0))
		close(fd_in);
	if ((!STDOUT) && (fd_out > 0))
		close(fd_out);
	return false;
}

/*
  decompress one file from the command line
*/
bool decompress_file(rzip_control *control)
{
	char *tmp, *tmpoutfile, *infilecopy = NULL;
	int fd_in, fd_out = -1, fd_hist = -1;
	i64 expected_size = 0, free_space;
	struct statvfs fbuf;

	if ( !STDIN ) {
		struct stat fdin_stat;
		infilecopy = strdupa(control->infile);
		if (unlikely(stat(infilecopy, &fdin_stat)))
			failure("File %s not found...\n", control->infile);
		else if (unlikely(!S_ISREG(fdin_stat.st_mode)))
			failure("lrzip-next only works on regular FILES\n");
		/* regardless, infilecopy has the input filename */
	}

	if (!STDOUT && !TEST_ONLY) {
		/* if output name already set, use it */
		if (control->outname)
			control->outfile = strdup(control->outname);
		else {
			/* default output name from infilecopy
			 * test if outdir specified. If so, strip path from filename of
			 * infilecopy, then remove suffix.
			*/
			if (control->outdir && (tmp = strrchr(infilecopy, '/')))
				tmpoutfile = strdupa(tmp + 1);
			else
				tmpoutfile = strdupa(infilecopy);

			/* remove suffix to make outfile name */
			if ((tmp = strrchr(tmpoutfile, '.')) && !strcmp(tmp, control->suffix))
				*tmp='\0';

			control->outfile = malloc((control->outdir == NULL? 0: strlen(control->outdir)) + strlen(tmpoutfile) + 1);
			if (unlikely(!control->outfile))
				fatal_return(("Failed to allocate outfile name\n"), false);

			if (control->outdir) {	/* prepend control->outdir */
				strcpy(control->outfile, control->outdir);
				strcat(control->outfile, tmpoutfile);
			} else
				strcpy(control->outfile, tmpoutfile);
		}

		if (!STDOUT)
			print_progress("Output filename is: %s\n", control->outfile);

		if (unlikely(!strcmp(control->outfile, infilecopy))) {
			control->flags |= FLAG_TEST_ONLY;	// stop and no more decompres or deleting files.
			fatal_return(("Output and Input files are the same...Cannot continue\n"), false);
		}
	}

	if (STDIN) {
		fd_in = open_tmpinfile(control);
		read_tmpinmagic(control);
		if (ENCRYPT)
			failure_return(("Cannot decompress encrypted file from STDIN\n"), false);
		expected_size = control->st_size;
		if (unlikely(!open_tmpinbuf(control)))
			return false;
	} else {
		fd_in = open(infilecopy, O_RDONLY);
		if (unlikely(fd_in == -1)) {
			fatal_return(("Failed to open %s\n", infilecopy), false);
		}
	}
	control->fd_in = fd_in;

	if (!(TEST_ONLY || STDOUT)) {
		fd_out = open(control->outfile, O_WRONLY | O_CREAT | O_EXCL, 0666);
		if (FORCE_REPLACE && (-1 == fd_out) && (EEXIST == errno)) {
			if (unlikely(unlink(control->outfile)))
				fatal_return(("Failed to unlink an existing file: %s\n", control->outfile), false);
			fd_out = open(control->outfile, O_WRONLY | O_CREAT | O_EXCL, 0666);
		}
		if (unlikely(fd_out == -1)) {
			/* We must ensure we don't delete a file that already
			 * exists just because we tried to create a new one */
			control->flags |= FLAG_KEEP_BROKEN;
			fatal_return(("Failed to create %s\n", control->outfile), false);
		}
		fd_hist = open(control->outfile, O_RDONLY);
		if (unlikely(fd_hist == -1))
			fatal_return(("Failed to open history file %s\n", control->outfile), false);

		/* Can't copy permissions from STDIN */
		if (!STDIN)
			if (unlikely(!preserve_perms(control, fd_in, fd_out)))
				return false;
	} else {
		fd_out = open_tmpoutfile(control);
		if (fd_out == -1) {
			fd_hist = -1;
		} else {
			fd_hist = open(control->outfile, O_RDONLY);
			if (unlikely(fd_hist == -1))
				fatal_return(("Failed to open history file %s\n", control->outfile), false);
			/* Unlink temporary file as soon as possible */
			if (unlikely(unlink(control->outfile)))
				fatal_return(("Failed to unlink tmpfile: %s\n", control->outfile), false);
		}
	}

// check for STDOUT removed. In memory compression speedup. No memory leak.
	if (unlikely(!open_tmpoutbuf(control)))
		return false;

	if (!STDIN) {
		if (unlikely(!read_magic(control, fd_in, &expected_size)))
			return false;
		if (unlikely(expected_size < 0))
			fatal_return(("Invalid expected size %'"PRId64"\n", expected_size), false);
	}

	if (!STDOUT) {
		/* Check if there's enough free space on the device chosen to fit the
		* decompressed or test file. */
		if (unlikely(fstatvfs(fd_out, &fbuf)))
			fatal_return(("Failed to fstatvfs in decompress_file\n"), false);
		free_space = (i64)fbuf.f_bsize * (i64)fbuf.f_bavail;
		if (free_space < expected_size) {
			if (FORCE_REPLACE && !TEST_ONLY)
				print_err("Warning, inadequate free space detected, but attempting to decompress file due to -f option being used.\n");
			else
				failure_return(("Inadequate free space to %s. Space needed: %'"PRId64". Space available: %'"PRId64".\nTry %s and \
select a larger volume.\n",
					TEST_ONLY ? "test file" : "decompress file. Use -f to override", expected_size, free_space,
					TEST_ONLY ? "setting `TMP=dirname`" : "using `-O dirname` or `-o [dirname/]filename` options"),
						false);
		}
	}
	control->fd_out = fd_out;
	control->fd_hist = fd_hist;

	if (NO_MD5)
		print_verbose("Not performing MD5 hash check\n");
	if (HAS_MD5)
		print_verbose("MD5 ");
	else
		print_verbose("CRC32 ");
	print_verbose("being used for integrity testing.\n");

	if (ENCRYPT && !control->salt_pass_len)		// Only get password if needed
		if (unlikely(!get_hash(control, 0)))
			return false;

	// vailidate file on decompression or test
	if (STDIN)
		print_err("Unable to validate a file from STDIN. To validate, check file directly.\n");
	else {
		print_progress("Validating file for consistency...");
		if (unlikely((get_fileinfo(control)) == false))
			failure_return(("File validation failed. Corrupt lrzip archive. Cannot continue\n"),false);
		print_progress("[OK]");
		if (!VERBOSE) print_progress("\n");	// output LF to prevent overwriing decompression output
	}
	show_version(control);	// show version here to preserve output formatting
	print_progress("Decompressing...");

	if (unlikely(runzip_fd(control, fd_in, fd_out, fd_hist, expected_size) < 0)) {
		clear_rulist(control);
		return false;
	}

	/* We can now safely delete sinfo and pthread data of all threads
	* created. */
	clear_rulist(control);

	if (STDOUT && !TMP_OUTBUF) {
		if (unlikely(!dump_tmpoutfile(control, fd_out)))
			return false;
	}

	/* if we get here, no fatal_return(( errors during decompression */
	print_progress("\r");
	if (!(STDOUT | TEST_ONLY))
		print_progress("Output filename is: %s: ", control->outfile);
	if (!expected_size)
		expected_size = control->st_size;
	if (!ENCRYPT)
		print_progress("[OK] - %'"PRId64" bytes                                \n", expected_size);
	else
		print_progress("[OK]                                             \n");

	if (TMP_OUTBUF)
		close_tmpoutbuf(control);

	if (fd_out > 0)
		if (unlikely(close(fd_hist) || close(fd_out)))
			fatal_return(("Failed to close files\n"), false);

	if (unlikely(!STDIN && !STDOUT && !TEST_ONLY && !preserve_times(control, fd_in)))
		return false;

	if ( !STDIN )
		close(fd_in);

	if (!KEEP_FILES && !STDIN)
		if (unlikely(unlink(control->infile)))
			fatal_return(("Failed to unlink %s\n", infilecopy), false);

	if (ENCRYPT)
		release_hashes(control);

	dealloc(control->outfile);
	return true;
}


bool initialise_control(rzip_control *control)
{
	time_t now_t, tdiff;
	char localeptr[] = "./", *eptr; 	/* for environment */
	size_t len;

	memset(control, 0, sizeof(rzip_control));
	control->msgout = stderr;
	control->msgerr = stderr;
	register_outputfile(control, control->msgout);
	control->flags = FLAG_SHOW_PROGRESS | FLAG_KEEP_FILES | FLAG_THRESHOLD;
	control->suffix = ".lrz";
	control->filter_flag = 0;		/* filter flag. Default to none */
	control->compression_level = 7;		/* compression level default */
	control->rzip_compression_level = 0;	/* rzip compression level default will equal compression level unless explicitly set */
	control->dictSize = 0;			/* Dictionary Size for lzma. 0 means program decides */
	control->ramsize = get_ram(control);	/* if something goes wrong, exit from get_ram */
	control->threshold = 100;		/* default for no threshold limiting */
	/* for testing single CPU */
	control->threads = PROCESSORS;		/* get CPUs for LZMA */
	control->page_size = PAGE_SIZE;
	control->nice_val = 19;

	/* The first 5 bytes of the salt is the time in seconds.
	 * The next 2 bytes encode how many times to hash the password.
	 * The last 9 bytes are random data, making 16 bytes of salt */
	if (unlikely((now_t = time(NULL)) == ((time_t)-1)))
		fatal_return(("Failed to call time in main\n"), false);
	if (unlikely(now_t < T_ZERO)) {
		print_output("Warning your time reads before the year 2011, check your system clock\n");
		now_t = T_ZERO;
	}
	/* Workaround for CPUs no longer keeping up with Moore's law!
	 * This way we keep the magic header format unchanged. */
	tdiff = (now_t - T_ZERO) / 4;
	now_t = T_ZERO + tdiff;
	control->secs = now_t;
	control->encloops = nloops(control->secs, control->salt, control->salt + 1);
	gcry_create_nonce(control->salt + 2, 6);

	/* Get Temp Dir. Try variations on canonical unix environment variable */
	eptr = getenv("TMPDIR");
	if (!eptr)
		eptr = getenv("TMP");
	if (!eptr)
		eptr = getenv("TEMPDIR");
	if (!eptr)
		eptr = getenv("TEMP");
	if (!eptr)
		eptr = localeptr;
	len = strlen(eptr);

	control->tmpdir = malloc(len + 2);
	if (control->tmpdir == NULL)
		fatal_return(("Failed to allocate for tmpdir\n"), false);
	strcpy(control->tmpdir, eptr);
	if (control->tmpdir[len - 1] != '/') {
		control->tmpdir[len] = '/'; 	/* need a trailing slash */
		control->tmpdir[len + 1] = '\0';
	}
	return true;
}
