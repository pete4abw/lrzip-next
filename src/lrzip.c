/*
   Copyright (C) 2006-2016,2018 Con Kolivas
   Copyright (C) 2011, 2022 Peter Hyman
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

#define MAGIC_LEN	(20)	// new v 0.9 magic header
#define MAGIC_V8_LEN	(18)	// new v 0.8 magic header
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
#elif defined(__OpenBSD__)
# include <sys/resource.h>
	struct rlimit rl;
	i64 ramsize = (i64)sysconf(_SC_PHYS_PAGES) * PAGE_SIZE;

	/* Raise limits all the way to the max */

	if (getrlimit(RLIMIT_DATA, &rl) == -1)
		fatal(("Failed to get limits in get_ram\n"), -1);

	rl.rlim_cur = rl.rlim_max;
	if (setrlimit(RLIMIT_DATA, &rl) == -1)
		fatal(("Failed to set limits in get_ram\n"), -1);

	/* Declare detected RAM to be either the max RAM available from
	physical memory or the max RAM allowed by RLIMIT_DATA, whatever
	is smaller, to prevent the heuristics from selecting
	compression windows which cause lrzip to go into deep swap */

	if (rl.rlim_max < ramsize)
		return rl.rlim_max;

	return ramsize;
#else /* __APPLE__ or __Open_BSD__ */
	i64 ramsize;
	FILE *meminfo;
	char aux[256];

	ramsize = (i64)sysconf(_SC_PHYS_PAGES) * PAGE_SIZE;
	if (ramsize <= 0) {
		/* Workaround for uclibc which doesn't properly support sysconf */
		if(!(meminfo = fopen("/proc/meminfo", "r")))
			fatal("Failed to open /proc/meminfo\n");

		while(!feof(meminfo) && !fscanf(meminfo, "MemTotal: %'"PRId64" kB", &ramsize)) {
			if (unlikely(fgets(aux, sizeof(aux), meminfo) == NULL)) {
				fclose(meminfo);
				fatal("Failed to fgets in get_ram\n");
			}
		}
		if (fclose(meminfo) == -1)
			fatal("Failed to close /proc/meminfo\n");
		ramsize *= 1024;
	}
#endif
	if (ramsize <= 0)
		fatal("No memory or can't determine ram? Can't continue.\n");
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
	if (ENCRYPT) {
		memcpy(&magic[6], &control->salt, 8);
		magic[15] = control->enc_code;	/* write whatever encryption code */
	}
	else if (control->eof) {
		i64 esize = htole64(control->st_size);	// we know file size even when piped
		memcpy(&magic[6], &esize, 8);
	}
	/* This is a flag that the archive contains an hash sum at the end
	 * which can be used as an integrity check instead of crc check.
	 * crc is still stored for compatibility with 0.5 versions.
	 */
	if (HAS_HASH)
		magic[14] = control->hash_code;	/* write whatever hash */

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
	} else if(BZIP3_COMPRESS) {
		/* Save block size. ZPAQ compression level is from 3 to 5, so this is sound.
		   bzip3 blocksize is from 1 to 8 (or 0 to 7). */
		magic[17] = 0b11111000 | (control->bzip3_bs - 1);
	}

	/* save compression levels
	 * high order bits, rzip compression level
	 * low order bits lrzip-next compression level
	 */
	magic[18] = (control->rzip_compression_level << 4) + control->compression_level;

	/* store comment length */
	magic[19] = (char) control->comment_length;

	if (unlikely(fdout_seekto(control, 0)))
		fatal("Failed to seek to BOF to write Magic Header\n");

	if (unlikely(put_fdout(control, magic, MAGIC_LEN) != MAGIC_LEN))
		fatal("Failed to write magic header\n");

	/* now write comment if any */
	if (magic[19]) {
		if (unlikely(put_fdout(control, control->comment, control->comment_length) != control->comment_length))
			fatal("Failed to write comment after magic header\n");
	}

	control->magic_written = 1;
	return true;
}

static inline i64 enc_loops(uchar b1, uchar b2)
{
	return (i64)b2 << (i64)b1;
}

// check for comments
// Called only if comment length > 0

static void  get_comment(rzip_control *control, int fd_in, unsigned char *magic)
{
	if (unlikely(!(control->comment = malloc(magic[19]+1))))
		fatal("Failed to allocate memory for comment\n");
	/* read comment */
	if (unlikely(read(fd_in, control->comment, magic[19]) != magic[19]))
		fatal("Failed to read comment\n");

	control->comment_length = magic[19];
	control->comment[control->comment_length] = '\0';
	return;
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

static void get_hash_from_magic(rzip_control *control, unsigned char *magic)
{
	/* Whether this archive contains hash data at the end or not */
	if (*magic > 0 && *magic <= MAXHASH)
	{
		control->flags |= FLAG_HASHED;
		control->hash_code = *magic;	/* set hash code */
		control->hash_label = &hashes[control->hash_code].label[0];
		control->hash_gcode = &hashes[control->hash_code].gcode;
		control->hash_len   = &hashes[control->hash_code].length;
	}
	else
		print_verbose("Unknown hash, falling back to CRC\n");

	return;
}

// get encrypted salt

static void get_encryption(rzip_control *control, unsigned char *magic, unsigned char *salt)
{
	if (*magic > 0 && *magic <= MAXENC) {
		control->flags |= FLAG_ENCRYPT;
		control->enc_code = *magic;
		/* In encrypted files, the size field is used to store the salt
		 * instead and the size is unknown, just like a STDOUT chunked
		 * file */
		memcpy(&control->salt, salt, 8);
		control->st_size = 0;
		control->encloops = enc_loops(control->salt[0], control->salt[1]);
	} else if (ENCRYPT) {
		print_err("Asked to decrypt a non-encrypted archive. Bypassing decryption. May fail!\n");
		control->flags &= ~FLAG_ENCRYPT;
		control->enc_code = 0;
	}
	control->enc_label  = &encryptions[control->enc_code].label[0];
	control->enc_gcode  = &encryptions[control->enc_code].gcode;
	control->enc_keylen = &encryptions[control->enc_code].keylen;
	control->enc_ivlen  = &encryptions[control->enc_code].ivlen;

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

// retrieve magic for lrzip v6

static void get_magic_v6(rzip_control *control, unsigned char *magic)
{
	// only difference is encryption

	if (!magic[22])	// not encrypted
		get_expected_size(control, magic);

	if (magic[16])	// lzma
		get_lzma_prop(control, &magic[16]);

	get_hash_from_magic(control, &magic[21]);
	get_encryption(control, &magic[22], &magic[6]);

	return;
}

// retrieve magic for lrzip-next v7

static void get_magic_v7(rzip_control *control, unsigned char *magic)
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

	get_hash_from_magic(control, &magic[22]);

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
	else if ((magic[17] & 0b11111000) == 0b11111000)
	{
		// bzip3 block size stuff.
		control->bzip3_bs = (magic[17] & 0b00000111) + 1;
	}
	else if (magic[17] & 0b10000000)	// zpaq block and compression level stored
	{
		control->zpaq_bs = magic[17] & 0b00001111;	// low order bits are block size
		magic[17] &= 0b01110000;			// strip high bit
		control->zpaq_level = magic[17] >> 4;		// divide by 16
	}

	get_hash_from_magic(control, &magic[14]);

	return;
}

// new lrzip-next v9 magic header format.

static void get_magic_v9(rzip_control *control, int fd_in, unsigned char *magic)
{
	/* get compression levels
	 * rzip level is high order bits
	 * lrzip level is low order bits
	 */
	control->compression_level = magic[18] & 0b00001111;
	control->rzip_compression_level = magic[18] >> 4;

	if (magic[19])	/* get comment if there is one */
		get_comment(control, fd_in, magic);

	return;
}
static bool get_magic(rzip_control *control, int fd_in, unsigned char *magic)
{
	memcpy(&control->major_version, &magic[4], 1);
	memcpy(&control->minor_version, &magic[5], 1);

	/* zero out compression levels so info does not show for earlier versions */
	control->rzip_compression_level = control->compression_level = 0;
	/* remove checks for lrzip < 0.6 */
	if (control->major_version == 0) {
		switch (control->minor_version) {
		case 6:
			get_magic_v6(control, magic);
			break;
		case 7:
			get_magic_v7(control, magic);
			break;
		case 8:
			get_magic_v8(control, magic);
			break;
		case 9:	/* version 0.9 adds two bytes */
			get_magic_v8(control, magic);
			get_magic_v9(control, fd_in, magic);
			break;
		default:
			print_err("lrzip version %d.%d archive is not supported. Aborting\n",
					control->major_version, control->minor_version);
			return false;
		}
	}

	return true;
}

static bool read_magic(rzip_control *control, int fd_in, i64 *expected_size)
{
	unsigned char magic[OLD_MAGIC_LEN];	// Make at least big enough for old magic
	int bytes_to_read;			// simplify reading of magic

	memset(magic, 0, sizeof(magic));
	/* Initially read only file type and version */
	if (unlikely(read(fd_in, magic, MAGIC_HEADER) != MAGIC_HEADER))
		fatal("Failed to read initial magic header\n");

	if (unlikely(strncmp(magic, "LRZI", 4)))
		fatal("Not an lrzip file\n");

	if (magic[4] == 0) {
		if (magic[5] < 8)		/* old magic */
			bytes_to_read = OLD_MAGIC_LEN;
		else if (magic[5] == 8) 	/* 0.8 file */
			bytes_to_read = MAGIC_V8_LEN;
		else				/* ASSUME current version */
			bytes_to_read = MAGIC_LEN;

		if (unlikely(read(fd_in, &magic[6], bytes_to_read - MAGIC_HEADER) != bytes_to_read - MAGIC_HEADER))
			fatal("Failed to read magic header\n");
	}

	if (unlikely(!get_magic(control, fd_in, magic)))
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
		fatal("Failed to fstat input file\n");
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
		fatal("Failed to fstat input file\n");
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
	control->outfile = realloc(NULL, strlen(control->tmpdir) + 16);
	if (unlikely(!control->outfile))
		fatal("Failed to allocate outfile name\n");
	strcpy(control->outfile, control->tmpdir);
	strcat(control->outfile, "lrzipout.XXXXXX");

	fd_out = mkstemp(control->outfile);
	if (fd_out == -1)
		fatal("Failed to create out tmpfile: %s\n", control->outfile);

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
		nmemb = len;
		ret = fwrite(offset_buf, 1, nmemb, control->outFILE);
		if (unlikely(ret != nmemb))
			fatal("Failed to fwrite %'"PRId32" bytes in fwrite_stdout\n", nmemb);
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
		nmemb = len;
		ret = write(control->fd_out, offset_buf, (size_t)nmemb);
		if (unlikely(ret != nmemb))
			fatal("Failed to write %'"PRId32" bytes to fd_out in write_fdout\n", nmemb);
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
		fatal("Failed: No temporary outfile created, unable to do in ram\n");
	/* flush anything not yet in the temporary file */
	fsync(fd_out);
	tmpoutfp = fdopen(fd_out, "r");
	if (unlikely(tmpoutfp == NULL))
		fatal("Failed to fdopen out tmpfile\n");
	rewind(tmpoutfp);

	if (!TEST_ONLY) {
		print_verbose("Dumping temporary file to control->outFILE.\n");
		while ((tmpchar = fgetc(tmpoutfp)) != EOF)
			putchar(tmpchar);
		fflush(control->outFILE);
		rewind(tmpoutfp);
	}

	if (unlikely(ftruncate(fd_out, 0)))
		fatal("Failed to ftruncate fd_out in dump_tmpoutfile\n");
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
		ret = len;
		ret = write(control->fd_in, offset_buf, (size_t)ret);
		if (unlikely(ret <= 0))
			fatal("Failed to write to fd_in in write_fdin\n");
		len -= ret;
		offset_buf += ret;
	}
	return true;
}

/* Open a temporary inputfile to perform stdin decompression */
int open_tmpinfile(rzip_control *control)
{
	int fd_in = -1;

	/* Use temporary directory if there is one. /tmp is default */
	control->infile = malloc(strlen(control->tmpdir) + 15);
	if (unlikely(!control->infile))
		fatal("Failed to allocate infile name\n");
	strcpy(control->infile, control->tmpdir);
	strcat(control->infile, "lrzipin.XXXXXX");
	fd_in = mkstemp(control->infile);

	if (fd_in == -1)
		fatal("Failed to create in tmpfile: %s\n", control->infile);

	register_infile(control, control->infile, (DECOMPRESS || TEST_ONLY) && STDIN);
	/* Unlink temporary file immediately to minimise chance of files left
	* lying around */
	if (unlikely(unlink(control->infile))) {
		close(fd_in);
		fatal("Failed to unlink tmpfile: %s\n", control->infile);
	}
	return fd_in;
}

static bool read_tmpinmagic(rzip_control *control, int fd_in)
{
	/* just in case < 0.8 file */
	char magic[OLD_MAGIC_LEN];
	int bytes_to_read, i, tmpchar;

	memset(magic, 0, sizeof(magic));
	/* Initially read only file type and version */
	for (i = 0; i < MAGIC_HEADER; i++) {
		tmpchar = getchar();
		if (unlikely(tmpchar == EOF))
			fatal("Reached end of file on STDIN prematurely on magic read\n");
		magic[i] = (char)tmpchar;
	}

	if (unlikely(strncmp(magic, "LRZI", 4)))
		fatal("Not an lrzip stream\n");

	if (magic[4] == 0) {
		if (magic[5] < 8)		/* old magic */
			bytes_to_read = OLD_MAGIC_LEN;
		else if (magic[5] == 8) 	/* 0.8 file */
			bytes_to_read = MAGIC_V8_LEN;
		else				/* ASSUME current version */
			bytes_to_read = MAGIC_LEN;

		for ( ; i < bytes_to_read; i++) {
			tmpchar = getchar();
			if (unlikely(tmpchar == EOF))
				fatal("Reached end of file on STDIN prematurely on magic read\n");
			magic[i] = (char)tmpchar;
		}
	}

	return get_magic(control, fd_in, magic);
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
		fatal("Failed to fdopen in tmpfile\n");

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
			fatal("Unable to even malloc 100MB for tmp_outbuf\n");
	}
	control->flags |= FLAG_TMP_OUTBUF;
	/* Allocate slightly more so we can cope when the buffer overflows and
	 * fall back to a real temporary file */
	control->out_maxlen = maxlen + control->page_size;
	control->tmp_outbuf = buf;
	if (!DECOMPRESS && !TEST_ONLY)
		control->out_ofs = control->out_len = MAGIC_LEN + control->comment_length;
	return true;
}

/* We've decided to use a temporary output file instead of trying to store
 * all the output buffer in ram so we can free up the ram and increase the
 * maximum sizes of ram we can allocate */
void close_tmpoutbuf(rzip_control *control)
{
	control->flags &= ~FLAG_TMP_OUTBUF;
	dealloc(control->tmp_outbuf);
	control->usable_ram = control->maxram += control->ramsize / 18;
}

static bool open_tmpinbuf(rzip_control *control)
{
	control->flags |= FLAG_TMP_INBUF;
	control->in_maxlen = control->maxram;
	control->tmp_inbuf = malloc(control->maxram + control->page_size);
	if (unlikely(!control->tmp_inbuf))
		fatal("Failed to malloc tmp_inbuf in open_tmpinbuf\n");
	return true;
}

void clear_tmpinbuf(rzip_control *control)
{
	control->in_len = control->in_ofs = 0;
}

bool clear_tmpinfile(rzip_control *control)
{
	if (unlikely(lseek(control->fd_in, 0, SEEK_SET)))
		fatal("Failed to lseek on fd_in in clear_tmpinfile\n");
	if (unlikely(ftruncate(control->fd_in, 0)))
		fatal("Failed to truncate fd_in in clear_tmpinfile\n");
	return true;
}

/* As per temporary output file but for input file */
void close_tmpinbuf(rzip_control *control)
{
	control->flags &= ~FLAG_TMP_INBUF;
	dealloc(control->tmp_inbuf);
	control->usable_ram = control->maxram += control->ramsize / 18;
}

static int get_pass(rzip_control *control, char *s)
{
	int len;

	memset(s, 0, PASS_LEN - SALT_LEN);
	if (control->passphrase)
		strncpy(s, control->passphrase, PASS_LEN - SALT_LEN - 1);
	else if (unlikely(fgets(s, PASS_LEN - SALT_LEN, stdin) == NULL))
		fatal("Failed to retrieve passphrase\n");
	len = strlen(s);
	if (len > 0 && ('\r' ==  s[len - 1] || '\n' == s[len - 1]))
		s[len - 1] = '\0';
	if (len > 1 && ('\r' ==  s[len - 2] || '\n' == s[len - 2]))
		s[len - 2] = '\0';
	len = strlen(s);
	if (unlikely(0 == len))
		fatal("Empty passphrase\n");
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
		dealloc(testphrase);
		dealloc(passphrase);
		dealloc(control->salt_pass);
		dealloc(control->hash);
		fatal("Failed to calloc encrypt buffers in get_hash\n");
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
			fatal("Failed to read encrypted header in get_header_info\n");
	}
	if (unlikely(read(fd_in, ctype, 1) != 1))
		fatal("Failed to read in get_header_info\n");

	*c_len = *u_len = *last_head = 0;
	/* remove checks for lrzip < 0.6 */
	if (control->major_version == 0) {
		// header the same after v 0.4 except for chunk bytes
		int read_len;

		read_len = chunk_bytes;
		if (unlikely(read(fd_in, c_len, read_len) != read_len))
			fatal("Failed to read in get_header_info\n");
		if (unlikely(read(fd_in, u_len, read_len) != read_len))
			fatal("Failed to read in get_header_info\n");
		if (unlikely(read(fd_in, last_head, read_len) != read_len))
			fatal("Failed to read_i64 in get_header_info\n");
		*c_len = le64toh(*c_len);
		*u_len = le64toh(*u_len);
		*last_head = le64toh(*last_head);
		if (ENCRYPT) {
			// decrypt header suppressing printing max verbose message
			if (unlikely(!decrypt_header(control, enc_head, ctype, c_len, u_len, last_head, LRZ_VALIDATE)))
				fatal("Failed to decrypt header in get_header_info\n");
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
	long double cratio, bpb;
	uchar ctype = 0;
	uchar save_ctype = 255;
	struct stat st;
	int fd_in;
	CLzmaProps p; // decode lzma header
	int lzma_ret;

	// Take out all STDIN checks
	struct stat fdin_stat;

	if (unlikely(stat(control->infile, &fdin_stat)))
		fatal("File %s not found...\n", control->infile);
	else if (unlikely(!S_ISREG(fdin_stat.st_mode)))
		fatal("File %s us not a regular file. lrzip-next cannot continue...\n", control->infile);
	else
		infilecopy = strdupa(control->infile);

	fd_in = open(infilecopy, O_RDONLY);
	if (unlikely(fd_in == -1))
		fatal("Failed to open %s\n", infilecopy);

	/* Get file size */
	if (unlikely(fstat(fd_in, &st)))
		fatal("bad magic file descriptor!?\n");
	infile_size = st.st_size;

	/* Get decompressed size */
	if (unlikely(!read_magic(control, fd_in, &expected_size)))
		goto error;

	if (INFO) show_version(control);		// show version if not validating

	if (ENCRYPT) {
		/* can only show info for current lrzip-next files */
		if (control->major_version == 0) {
			if (control->minor_version < 8)
				fatal("Cannot show info for earlier encrypted lrzip/lrzip-next files: version %d.%d\n",
						control->major_version, control->minor_version);
			if (!control->salt_pass_len)		// Only get password if needed
				if (unlikely(!get_hash(control, 0)))
					return false;
		}
	}

	/* remove checks for lrzip < 0.6 */
	if (control->major_version == 0) {
		if (unlikely(read(fd_in, &chunk_byte, 1) != 1))
			fatal("Failed to read chunk_byte in get_fileinfo\n");
		if (unlikely(chunk_byte < 1 || chunk_byte > 8))
			fatal("Invalid chunk bytes %'d\n", chunk_byte);
		if (unlikely(read(fd_in, &control->eof, 1) != 1))
			fatal("Failed to read eof in get_fileinfo\n");
		if (!ENCRYPT) {
			if (unlikely(read(fd_in, &chunk_size, chunk_byte) != chunk_byte))
				fatal("Failed to read chunk_size in get_fileinfo\n");
			chunk_size = le64toh(chunk_size);
			if (unlikely(chunk_size < 0))
				fatal("Invalid chunk size %'"PRId64"\n", chunk_size);
			/* set header offsets for earlier versions */
			switch (control->minor_version) {
				case 6:
				case 7:	ofs = 26;
					break;
				case 8: ofs = 20;
					break;
				case 9: ofs = 22 + control->comment_length;	/* comment? Add length */
					break;
			}
			ofs += chunk_byte;
			/* header length is the same for non-encrypted files */
			header_length = 1 + (chunk_byte * 3);
		} else {	/* ENCRYPTED */
			chunk_byte=8;		// chunk byte size is always 8 for encrypted files
			chunk_size=0;		// chunk size is unknown with encrypted files
			header_length=33;	// 25 + 8
			// salt is first 8 bytes
			if (control->major_version == 0) {
				switch (control->minor_version) {
					case 8:	ofs = 20;
						break;
					case 9: ofs = 22 + control->comment_length;
						break;
					default: fatal("Cannot decrypt earlier versions of lrzip-next\n");
						 break;
				}
			}
		}
	}

next_chunk:
	stream = 0;
	stream_head[0] = 0;
	stream_head[1] = stream_head[0] + header_length;

	if (!ENCRYPT) {
		chunk_total += chunk_size;
		if (unlikely(chunk_byte && (chunk_byte > 8 || chunk_size <= 0)))
			fatal("Invalid chunk data\n");
	}

	if (INFO) {
		print_verbose("Rzip chunk:       %'d\n", ++chunk);
		print_verbose("Chunk byte width: %'d\n", chunk_byte);
		print_verbose("Chunk size:       ");
		if (!ENCRYPT)
			print_verbose("%'"PRId64"\n", chunk_size);
		else
			print_verbose("N/A %s Encrypted File\n", control->enc_label);
	}
	while (stream < NUM_STREAMS) {
		int block = 1;

		second_last = 0;
		if (unlikely(lseek(fd_in, stream_head[stream] + ofs, SEEK_SET) == -1))
			fatal("Failed to seek to header data in get_fileinfo\n");

		if (unlikely(!get_header_info(control, fd_in, &ctype, &c_len, &u_len, &last_head, chunk_byte)))
			return false;

		if (ENCRYPT && ctype != CTYPE_NONE)
			fatal("Invalid stream ctype (%02x) for encrypted file. Bad Password?\n", ctype);

		if (INFO) {
			print_verbose("Stream: %'d\n", stream);
			print_maxverbose("Offset: %'"PRId64"\n", stream_head[stream] + ofs);
			print_verbose("%s\t%s\t%s\t%16s / %14s", "Block","Comp","Percent","Comp Size", "UComp Size");
			print_maxverbose("%18s : %14s", "Offset", "Head");
			print_verbose("\n");
		}
		do {
			i64 head_off;

			if (unlikely(last_head && last_head <= second_last))
				fatal("Invalid earlier last_head position, corrupt archive.\n");
			second_last = last_head;
			if (!ENCRYPT) {
				if (unlikely(last_head + ofs > infile_size))
					fatal("Offset greater than archive size, likely corrupted/truncated archive.\n");
			} else {
				if (unlikely(last_head + ofs + header_length > infile_size))
					fatal("Offset greater than archive size, likely corrupted/truncated archive.\n");
			}

			if (unlikely((head_off = lseek(fd_in, last_head + ofs, SEEK_SET)) == -1))
				fatal("Failed to seek to header data in get_fileinfo\n");
			if (unlikely(!get_header_info(control, fd_in, &ctype, &c_len, &u_len,
					&last_head, chunk_byte)))
				return false;
			if (unlikely(last_head < 0 || c_len < 0 || u_len < 0))
				fatal("Entry negative, likely corrupted archive.\n");
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
			} else if (ctype == CTYPE_BZIP3) {
				if (INFO) print_verbose("bzip3");
			} else
				fatal("Unknown Compression Type: %'d\n", ctype);
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
		fatal("Failed to lseek c_len in get_fileinfo\n");

	if (ofs >= infile_size - *control->hash_len)
		goto done;
	else if (ENCRYPT)
		if (ofs+header_length+ *control->hash_len > infile_size)
			goto done;

	/* Chunk byte entry */
	/* remove checks for lrzip < 0.6 */
	if (control->major_version == 0) {
		if(!ENCRYPT) {
			if (unlikely(read(fd_in, &chunk_byte, 1) != 1))
				fatal("Failed to read chunk_byte in get_fileinfo\n");
			if (unlikely(chunk_byte < 1 || chunk_byte > 8))
				fatal("Invalid chunk bytes %'d\n", chunk_byte);
			ofs++;
			if (unlikely(read(fd_in, &control->eof, 1) != 1))
				fatal("Failed to read eof in get_fileinfo\n");
			if (unlikely(read(fd_in, &chunk_size, chunk_byte) != chunk_byte))
				fatal("Failed to read chunk_size in get_fileinfo\n");
			chunk_size = le64toh(chunk_size);
			if (unlikely(chunk_size < 0))
				fatal("Invalid chunk size %'"PRId64"\n", chunk_size);
			ofs += 1 + chunk_byte;
			header_length = 1 + (chunk_byte * 3);
		}
		else {
			// ENCRYPTED
			// no change to chunk_byte
			ofs+=10;
			// no change to header_length
		}
	}

	goto next_chunk;
done:
	/* compression ratio and bits per byte ratio */
	cratio = (long double)expected_size / (long double)infile_size;
	bpb = ((long double)infile_size / (long double)expected_size) * 8;
	if (unlikely(ofs > infile_size))
		fatal("Offset greater than archive size, likely corrupted/truncated archive.\n");

	if (INFO) {
		print_output("\nSummary\n=======\n");
		print_output("File: %s\nlrzip-next version: %'d.%'d ", infilecopy,
				control->major_version, control->minor_version, ENCRYPT ? "Encrypted " : "");
		if (ENCRYPT)
			print_output("%s Encrypted ", control->enc_label);
		print_output("file\n");
		if (control->comment_length)	/* print comment */
			print_output("Archive Comment: %s\n", control->comment);

		print_output("Compression Method: ");
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
		else if (save_ctype == BZIP3_COMPRESS) {
			print_output("rzip + bzip3 -- Block Size: %d", (1 << control->bzip3_bs) * ONE_MB);
		}
		else
			print_output("Dunno wtf\n");

		/* only print stored compression level for versions that have it! */
		if (control->compression_level)
			print_output("Rzip Compression Level: %d, Lrzip-next Compressinn Level: %d\n",
				control->rzip_compression_level, control->compression_level);
		/* show filter used */
		if (FILTER_USED) {
			print_output("Filter Used: %s",
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

		if (!expected_size)
			print_output("Due to using %s, expected decompression size not available\n",
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

		if (expected_size) {
			print_output("\n  Decompressed file size: %'14"PRIu64"\n", expected_size);
			print_output("  Compressed file size:   %'14"PRIu64"\n", infile_size);
			print_output("  Compression ratio:      %14.3Lfx, bpb: %.3Lf\n", cratio, bpb);
		} else {
			print_output("  Decompressed file size:    Unavailable\n");
			print_output("  Compressed file size:   %'14"PRIu64"\n", infile_size);
			print_output("  Compression ratio:         Unavailable\n");
		}
	} /* end if (INFO) */

	if (HAS_HASH) {
		uchar *hash_stored;

		int i;

		if (INFO) {
			hash_stored = calloc(*control->hash_len, 1);
			if (unlikely(lseek(fd_in, - *control->hash_len, SEEK_END) == -1))
				fatal("Failed to seek to %s data in get_fileinfo.\n", control->hash_label);
			if (unlikely(read(fd_in, hash_stored, *control->hash_len) != *control->hash_len))
				fatal("Failed to read %s data in get_fileinfo.\n", control->hash_label);
			if (ENCRYPT)
				if (unlikely(!lrz_decrypt(control, hash_stored, *control->hash_len, control->salt_pass, LRZ_VALIDATE)))
					fatal("Failure decrypting %s in get_fileinfo.\n", control->hash_label);
			print_output("\n  %s Checksum: ", control->hash_label);
			for (i = 0; i < *control->hash_len; i++)
				print_output("%02x", hash_stored[i]);
			print_output("\n");
			dealloc(hash_stored);
		}
	} else {
		if (INFO) print_output("\n  CRC32 used for integrity testing\n");
	}

out:
	if (unlikely(close(fd_in)))
		fatal("Failed to close fd_in in get_fileinfo\n");
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
	int fd_in = -1, fd_out = -1, len = MAGIC_LEN + control->comment_length;
	char *header;

	header = calloc(len, 1);

	control->flags |= FLAG_HASHED;
	/* allocate result block for selected hash */
	control->hash_resblock = calloc (*control->hash_len, 1);

	if (ENCRYPT) {			/* AES 128 now default */
		if (unlikely(!get_hash(control, 1)))
			return false;
	}

	if (!STDIN) {
		fd_in = open(control->infile, O_RDONLY);
		if (unlikely(fd_in == -1))
			fatal("Failed to open %s\n", control->infile);
	} else
		fd_in = fileno(control->inFILE);

	if (!STDOUT) {
		if (control->outname) {
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
				fatal("Failed to allocate outfile name\n");

			if (control->outdir) {	/* prepend control->outdir */
				strcpy(control->outfile, control->outdir);
				strcat(control->outfile, tmpinfile);
			} else
				strcpy(control->outfile, tmpinfile);
			strcat(control->outfile, control->suffix);
			// print_progress("Output filename is: %s\n", control->outfile);
			// Not needed since printed at end of decompression
		}

		if (!strcmp(control->infile, control->outfile))
			fatal("Input and Output files are the same. %s. Exiting\n",control->infile);

		fd_out = open(control->outfile, O_RDWR | O_CREAT | O_EXCL, 0666);
		if (FORCE_REPLACE && (-1 == fd_out) && (EEXIST == errno)) {
			if (unlikely(unlink(control->outfile)))
				fatal("Failed to unlink an existing file: %s\n", control->outfile);
			fd_out = open(control->outfile, O_RDWR | O_CREAT | O_EXCL, 0666);
		}
		if (unlikely(fd_out == -1)) {
			/* We must ensure we don't delete a file that already
			 * exists just because we tried to create a new one */
			control->flags |= FLAG_KEEP_BROKEN;
			fatal("Failed to create %s\n", control->outfile);
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
	if (unlikely(!STDOUT && write(fd_out, header, len) != len))
		fatal("Cannot write file header\n");

	rzip_fd(control, fd_in, fd_out);

	/* need to write magic after compression for expected size */
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
		fatal("Failed to close fd_out\n");
	if (TMP_OUTBUF)
		close_tmpoutbuf(control);

	if (!KEEP_FILES && !STDIN) {
		if (unlikely(unlink(control->infile)))
			fatal("Failed to unlink %s\n", control->infile);
	}

	dealloc(control->outfile);
	dealloc(control->hash_resblock);
	dealloc(header);
	return true;
error:
	dealloc(header);
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
			fatal("File %s not found...\n", control->infile);
		else if (unlikely(!S_ISREG(fdin_stat.st_mode)))
			fatal("lrzip-next only works on regular FILES\n");
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
				fatal("Failed to allocate outfile name\n");

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
			fatal("Output and Input files are the same...Cannot continue\n");
		}
	}

	if (STDIN) {
		fd_in = open_tmpinfile(control);
		read_tmpinmagic(control, fd_in);
		if (ENCRYPT)
			fatal("Cannot decompress encrypted file from STDIN\n");
		expected_size = control->st_size;
		if (unlikely(!open_tmpinbuf(control)))
			return false;
	} else {
		fd_in = open(infilecopy, O_RDONLY);
		if (unlikely(fd_in == -1)) {
			fatal("Failed to open %s\n", infilecopy);
		}
	}
	control->fd_in = fd_in;

	if (!(TEST_ONLY || STDOUT)) {
		fd_out = open(control->outfile, O_WRONLY | O_CREAT | O_EXCL, 0666);
		if (FORCE_REPLACE && (-1 == fd_out) && (EEXIST == errno)) {
			if (unlikely(unlink(control->outfile)))
				fatal("Failed to unlink an existing file: %s\n", control->outfile);
			fd_out = open(control->outfile, O_WRONLY | O_CREAT | O_EXCL, 0666);
		}
		if (unlikely(fd_out == -1)) {
			/* We must ensure we don't delete a file that already
			 * exists just because we tried to create a new one */
			control->flags |= FLAG_KEEP_BROKEN;
			fatal("Failed to create %s\n", control->outfile);
		}
		fd_hist = open(control->outfile, O_RDONLY);
		if (unlikely(fd_hist == -1))
			fatal("Failed to open history file %s\n", control->outfile);

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
				fatal("Failed to open history file %s\n", control->outfile);
			/* Unlink temporary file as soon as possible */
			if (unlikely(unlink(control->outfile)))
				fatal("Failed to unlink tmpfile: %s\n", control->outfile);
		}
	}

// check for STDOUT removed. In memory compression speedup. No memory leak.
	if (unlikely(!open_tmpoutbuf(control)))
		return false;

	if (!STDIN) {
		if (unlikely(!read_magic(control, fd_in, &expected_size)))
			return false;
		if (unlikely(expected_size < 0))
			fatal("Invalid expected size %'"PRId64"\n", expected_size);
	}

	if (!STDOUT) {
		/* Check if there's enough free space on the device chosen to fit the
		* decompressed or test file. */
		if (unlikely(fstatvfs(fd_out, &fbuf)))
			fatal("Failed to fstatvfs in decompress_file\n");
		free_space = (i64)fbuf.f_bsize * (i64)fbuf.f_bavail;
		if (free_space < expected_size) {
			if (FORCE_REPLACE && !TEST_ONLY)
				print_err("Warning, inadequate free space detected, but attempting to decompress file due to -f option being used.\n");
			else
				fatal("Inadequate free space to %s. Space needed: %'"PRId64". Space available: %'"PRId64".\nTry %s and select a larger volume.\n",
					TEST_ONLY ? "test file" : "decompress file. Use -f to override", expected_size, free_space,
					TEST_ONLY ? "setting `TMP=dirname`" : "using `-O dirname` or `-o [dirname/]filename` options");
		}
	}
	control->fd_out = fd_out;
	control->fd_hist = fd_hist;

	show_version(control);

	if (NO_HASH)
		print_verbose("Not performing hash check\n");
	if (HAS_HASH)
		print_verbose("%s ", control->hash_label);
	else
		print_verbose("CRC32 ");
	print_verbose("being used for integrity testing.\n");

	control->hash_resblock = calloc(*control->hash_len, 1);

	if (ENCRYPT && !control->salt_pass_len) {	// Only get password if needed
		if (unlikely(!get_hash(control, 0)))
			return false;
		print_maxverbose("Encryption hash loops %'"PRId64"\n", control->encloops);
		if (!INFO)
			print_verbose("%s Encryption Used\n", control->enc_label);
	}

	// vailidate file on decompression or test
	if (STDIN)
		print_err("Unable to validate a file from STDIN. To validate, check file directly.\n");
	else {
		print_progress("Validating file for consistency...");
		if (unlikely((get_fileinfo(control)) == false))
			fatal("File validation failed. Corrupt lrzip archive. Cannot continue\n");
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
	if (!(STDOUT || TEST_ONLY))
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
			fatal("Failed to close files\n");

	if (unlikely(!STDIN && !STDOUT && !TEST_ONLY && !preserve_times(control, fd_in)))
		return false;

	if ( !STDIN )
		close(fd_in);

	if (!KEEP_FILES && !STDIN)
		if (unlikely(unlink(control->infile)))
			fatal("Failed to unlink %s\n", infilecopy);

	if (ENCRYPT)
		release_hashes(control);

	dealloc(control->outfile);
	dealloc(control->hash_resblock);
	return true;
}


bool initialise_control(rzip_control *control)
{
	time_t now_t, tdiff;
	char localeptr[] = "/tmp", *eptr; 	/* for environment. OR Default to /tmp if none set */
	size_t len;

	memset(control, 0, sizeof(rzip_control));
	control->locale = "";			/* empty string for default locale */
	control->msgout = stderr;
	control->msgerr = stderr;
	register_outputfile(control, control->msgout);
	control->flags = FLAG_SHOW_PROGRESS | FLAG_KEEP_FILES | FLAG_THRESHOLD;
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
		fatal("Failed to call time in main\n");
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
		fatal("Failed to allocate for tmpdir\n");
	strcpy(control->tmpdir, eptr);
	if (control->tmpdir[len - 1] != '/') {
		control->tmpdir[len] = '/'; 	/* need a trailing slash */
		control->tmpdir[len + 1] = '\0';
	}

	/* just in case, set pointers for hash and encryptions */

	return true;
}
