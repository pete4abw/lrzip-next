/*
   Copyright (C) 2006-2016,2018 Con Kolivas
   Copyright (C) 2011, 2019-2021  Peter Hyman
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

#ifndef LRZIP_PRIV_H
#define LRZIP_PRIV_H

#include "config.h"

#define NUM_STREAMS 2
#define ONE_MB 1048576
#define one_g (1000 * ONE_MB)
#define STREAM_BUFSIZE (ONE_MB * 10)

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <semaphore.h>
#include <gcrypt.h>
#include <inttypes.h>

#ifdef HAVE_PTHREAD_H
# include <pthread.h>
#endif

#ifdef HAVE_STRING_H
# include <string.h>
#endif

#ifdef HAVE_MALLOC_H
# include <malloc.h>
#endif

#ifdef HAVE_ALLOCA_H
# include <alloca.h>
#elif defined __GNUC__
# define alloca __builtin_alloca
#elif defined _AIX
# define alloca __alloca
#elif defined _MSC_VER
# include <malloc.h>
# define alloca _alloca
#else
# include <stddef.h>
# ifdef  __cplusplus
extern "C"
# endif
void *alloca (size_t);
#endif

#ifdef HAVE_ENDIAN_H
# include <endian.h>
#elif HAVE_SYS_ENDIAN_H
# include <sys/endian.h>
#endif
#ifndef __BYTE_ORDER
# ifndef __BIG_ENDIAN
#  define __BIG_ENDIAN	4321
#  define __LITTLE_ENDIAN	1234
# endif
# ifdef WORDS_BIGENDIAN
#  define __BYTE_ORDER __BIG_ENDIAN
# else
#  define __BYTE_ORDER __LITTLE_ENDIAN
# endif
#endif

/* This block enumerates gcrypt constants based on magic header */

enum hashcodes {CRC=0, MD5, RIPEMD, SHA256, SHA384, SHA512, SHA3_256, SHA3_512,
	SHAKE128_16, SHAKE128_32, SHAKE128_64, SHAKE256_16, SHAKE256_32, SHAKE256_64};

enum enccodes {NONE=0, AES128, AES256};

#define MAXHASH 13
#define MAXENC   2

extern struct hash {
	char	*label;	/* string label */
	int	mcode;	/* magic header code */
	int	gcode;	/* gcrypt hash code */
	int	length;	/* hash length  0 for XOF function */
} hashes[];

extern struct encryption {
	char	*label;	/* string label */
	int	mcode;	/* magic header code */
	int	gcode;	/* gcrypt hash code */
	int	keylen;	/* key length */
	int	ivlen;	/* where applicable */
} encryptions[];

#define free(X) do { free((X)); (X) = NULL; } while (0)

#ifndef strdupa
# define strdupa(str) strcpy(alloca(strlen(str) + 1), str)
#endif

#ifndef strndupa
# define strndupa(str, len) strncpy(alloca(len + 1), str, len)
#endif

#ifndef uchar
#define uchar unsigned char
#endif

#ifndef int32
#if (SIZEOF_INT == 4)
#define int32 int
#elif (SIZEOF_LONG == 4)
#define int32 long
#elif (SIZEOF_SHORT == 4)
#define int32 short
#endif
#endif

#ifndef int16
#if (SIZEOF_INT == 2)
#define int16 int
#elif (SIZEOF_SHORT == 2)
#define int16 short
#endif
#endif

#ifndef uint32
#define uint32 unsigned int32
#endif

#ifndef uint16
#define uint16 unsigned int16
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b)? (a): (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b)? (a): (b))
#endif

#if !HAVE_STRERROR
extern char *sys_errlist[];
#define strerror(i) sys_errlist[i]
#endif

#ifndef HAVE_ERRNO_H
extern int errno;
#endif

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)
#define __maybe_unused	__attribute__((unused))

#if defined(__MINGW32__) || defined(__CYGWIN__) || defined(__ANDROID__) || defined(__APPLE__) || defined(__OpenBSD__)
# define ffsll __builtin_ffsll
#endif

typedef int64_t i64;
typedef uint32_t u32;

typedef struct rzip_control rzip_control;

/* ck specific unnamed semaphore implementations to cope with osx not
 * implementing them. */
#ifdef __APPLE__
struct cksem {
	int pipefd[2];
};

typedef struct cksem cksem_t;
#else
typedef sem_t cksem_t;
#endif

#if !defined(__linux)
 #define mremap fake_mremap
#endif

/* Since 2011 Apple was prohibited from
 * computing MD5 because of some unknown
 * fault. Not anymore using libgcrypt
 */
//#if defined(__APPLE__)
//# define MD5_RELIABLE (0)
//#else
//# define MD5_RELIABLE (1)
//#endif

#define bswap_32(x) \
     ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) |		      \
      (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))

# define bswap_64(x) \
     ((((x) & 0xff00000000000000ull) >> 56)				      \
      | (((x) & 0x00ff000000000000ull) >> 40)				      \
      | (((x) & 0x0000ff0000000000ull) >> 24)				      \
      | (((x) & 0x000000ff00000000ull) >> 8)				      \
      | (((x) & 0x00000000ff000000ull) << 8)				      \
      | (((x) & 0x0000000000ff0000ull) << 24)				      \
      | (((x) & 0x000000000000ff00ull) << 40)				      \
      | (((x) & 0x00000000000000ffull) << 56))

#ifdef leto32h
# define le32toh(x) leto32h(x)
# define le64toh(x) leto64h(x)
#endif

#ifndef le32toh
# if __BYTE_ORDER == __LITTLE_ENDIAN
#  define htole32(x) (x)
#  define le32toh(x) (x)
#  define htole64(x) (x)
#  define le64toh(x) (x)
# elif __BYTE_ORDER == __BIG_ENDIAN
#  define htole32(x) bswap_32 (x)
#  define le32toh(x) bswap_32 (x)
#  define htole64(x) bswap_64 (x)
#  define le64toh(x) bswap_64 (x)
#else
#error UNKNOWN BYTE ORDER
#endif
#endif

#define LZMA_LC_LP_PB		0x5D
#define LZMA_LC			3
#define LZMA_LP			0
#define LZMA_PB			2
/* from Lzma2Dec.c. Decode Dictionary */
#define LZMA2_DIC_SIZE_FROM_PROP(p) (p == 40 ? 0xFFFFFFFF : (((u32)2 | ((p) & 1)) << ((p) / 2 + 11)))
/* from Lzma2Enc.c. Convert Dicsize to lzma2 encoding */
static inline unsigned char lzma2_prop_from_dic(u32 dicSize)
{
	unsigned i;
	for (i = 0; i <= 40; i++)
		if (dicSize <= LZMA2_DIC_SIZE_FROM_PROP(i))
			break;
	return (unsigned char)i;
}
/* re-purposed for bzip3
 * This value will be the actual block size from 32MB to 512MB - 1 */
#define BZIP3_BLOCK_SIZE_FROM_PROP(p) (p == 8 ? 0x1FF00000 : (((u32)2 | ((p) & 1)) << ((p) / 2 + 24)))
static inline unsigned char bzip3_prop_from_block_size(u32 bs)
{
	unsigned i;
	for (i = 0; i <= 8; i++)
		if (bs <= BZIP3_BLOCK_SIZE_FROM_PROP(i))
			break;
	return (unsigned char)i;
}
#define FLAG_SHOW_PROGRESS	(1 << 0)
#define FLAG_KEEP_FILES		(1 << 1)
#define FLAG_TEST_ONLY		(1 << 2)
#define FLAG_FORCE_REPLACE	(1 << 3)
#define FLAG_DECOMPRESS		(1 << 4)
#define FLAG_NO_COMPRESS	(1 << 5)
#define FLAG_LZO_COMPRESS	(1 << 6)
#define FLAG_BZIP2_COMPRESS	(1 << 7)
#define FLAG_ZLIB_COMPRESS	(1 << 8)
#define FLAG_ZPAQ_COMPRESS	(1 << 9)
#define FLAG_VERBOSITY		(1 << 10)
#define FLAG_VERBOSITY_MAX	(1 << 11)
#define FLAG_STDIN		(1 << 12)
#define FLAG_STDOUT		(1 << 13)
#define FLAG_INFO		(1 << 14)
#define FLAG_UNLIMITED		(1 << 15)
#define FLAG_HASH		(1 << 16)
#define FLAG_HASHED		(1 << 17)
#define FLAG_CHECK		(1 << 18)
#define FLAG_KEEP_BROKEN	(1 << 19)
#define FLAG_THRESHOLD		(1 << 20)
#define FLAG_TMP_OUTBUF		(1 << 21)
#define FLAG_TMP_INBUF		(1 << 22)
#define FLAG_ENCRYPT		(1 << 23)
#define FLAG_BZIP3_COMPRESS	(1 << 24)

#define NO_HASH		(!(HASH_CHECK) && !(HAS_HASH))

#define CTYPE_NONE 3
#define CTYPE_BZIP2 4
#define CTYPE_LZO 5
#define CTYPE_LZMA 6
#define CTYPE_GZIP 7
#define CTYPE_ZPAQ 8
#define CTYPE_BZIP3 9

#define PASS_LEN 512
#define HASH_LEN 64
#define SALT_LEN 8

#define LRZ_DECRYPT	(0)
#define LRZ_ENCRYPT	(1)
#define LRZ_VALIDATE	(2)	//to suppress printing decompress message when showing info or validating a file

#if defined(NOTHREAD) || !defined(_SC_NPROCESSORS_ONLN)
# define PROCESSORS (1)
#else
# define PROCESSORS (sysconf(_SC_NPROCESSORS_ONLN))
#endif

#ifndef PAGE_SIZE
# ifdef _SC_PAGE_SIZE
#  define PAGE_SIZE (sysconf(_SC_PAGE_SIZE))
# else
#  define PAGE_SIZE (4096)
# endif
#endif

#define dealloc(ptr) do { \
	free(ptr); \
	ptr = NULL; \
} while (0)

/* Determine how many times to hash the password when encrypting, based on
 * the date such that we increase the number of loops according to Moore's
 * law relative to when the data is encrypted. It is then stored as a two
 * byte value in the header */
#define MOORE 1.835          // world constant  [TIMES per YEAR]
#define ARBITRARY  1000000   // number of sha2 calls per one second in 2011
#define T_ZERO 1293840000    // seconds since epoch in 2011

#define SECONDS_IN_A_YEAR (365*86400)
#define MOORE_TIMES_PER_SECOND pow (MOORE, 1.0 / SECONDS_IN_A_YEAR)
#define ARBITRARY_AT_EPOCH (ARBITRARY * pow (MOORE_TIMES_PER_SECOND, -T_ZERO))

#define FLAG_VERBOSE (FLAG_VERBOSITY | FLAG_VERBOSITY_MAX)
#define FLAG_NOT_LZMA (FLAG_NO_COMPRESS | FLAG_LZO_COMPRESS | FLAG_BZIP2_COMPRESS | FLAG_ZLIB_COMPRESS | FLAG_ZPAQ_COMPRESS | FLAG_BZIP3_COMPRESS)
#define LZMA_COMPRESS	(!(control->flags & FLAG_NOT_LZMA))

#define SHOW_PROGRESS	(control->flags & FLAG_SHOW_PROGRESS)
#define KEEP_FILES	(control->flags & FLAG_KEEP_FILES)
#define TEST_ONLY	(control->flags & FLAG_TEST_ONLY)
#define FORCE_REPLACE	(control->flags & FLAG_FORCE_REPLACE)
#define DECOMPRESS	(control->flags & FLAG_DECOMPRESS)
#define NO_COMPRESS	(control->flags & FLAG_NO_COMPRESS)
#define LZO_COMPRESS	(control->flags & FLAG_LZO_COMPRESS)
#define BZIP2_COMPRESS	(control->flags & FLAG_BZIP2_COMPRESS)
#define ZLIB_COMPRESS	(control->flags & FLAG_ZLIB_COMPRESS)
#define ZPAQ_COMPRESS	(control->flags & FLAG_ZPAQ_COMPRESS)
#define BZIP3_COMPRESS	(control->flags & FLAG_BZIP3_COMPRESS)
#define VERBOSE		(control->flags & FLAG_VERBOSE)
#define VERBOSITY	(control->flags & FLAG_VERBOSITY)
#define MAX_VERBOSE	(control->flags & FLAG_VERBOSITY_MAX)
#define STDIN		(control->flags & FLAG_STDIN)
#define STDOUT		(control->flags & FLAG_STDOUT)
#define INFO		(control->flags & FLAG_INFO)
#define UNLIMITED	(control->flags & FLAG_UNLIMITED)
#define HASH_CHECK	(control->flags & FLAG_HASH)
#define HAS_HASH	(control->flags & FLAG_HASHED)
#define CHECK_FILE	(control->flags & FLAG_CHECK)
#define KEEP_BROKEN	(control->flags & FLAG_KEEP_BROKEN)
#define LZ4_TEST	(control->flags & FLAG_THRESHOLD)
#define TMP_OUTBUF	(control->flags & FLAG_TMP_OUTBUF)
#define TMP_INBUF	(control->flags & FLAG_TMP_INBUF)
#define ENCRYPT		(control->flags & FLAG_ENCRYPT)

/* Filter flags
 * 0 = none
 * 1 = x86 filter
 * 2 = ASM
 * 3 = ASMT
 * 4 = PPC
 * 5 = SPARC
 * 6 = IA64
 * 7 = Delta
*/
#define FILTER_FLAG_X86		1
#define FILTER_FLAG_ARM		2
#define FILTER_FLAG_ARMT	3
#define FILTER_FLAG_PPC		4
#define FILTER_FLAG_SPARC	5
#define FILTER_FLAG_IA64	6
#define FILTER_FLAG_DELTA	7
#define DEFAULT_DELTA		1				// delta diff is 1 by default
#define FILTER_USED		(control->filter_flag & 7) 	// lazy for OR of the above 0111b
#define FILTER_NOT_USED		(!FILTER_USED)
#define FILTER_MASK		0b00000111			// decode magic
#define DELTA_OFFSET_MASK	0b11111000

struct sliding_buffer {
	uchar *buf_low;	/* The low window buffer */
	uchar *buf_high;/* "" high "" */
	i64 orig_offset;/* Where the original buffer started */
	i64 offset_low;	/* What the current offset the low buffer has */
	i64 offset_high;/* "" high buffer "" */
	i64 offset_search;/* Where the search is up to */
	i64 orig_size;	/* How big the full buffer would be */
	i64 size_low;	/* How big the low buffer is */
	i64 size_high;	/* "" high "" */
	i64 high_length;/* How big the high buffer should be */
	int fd;		/* The fd of the mmap */
};

struct checksum {
//	uint32_t *cksum;
	uchar *buf;
	i64 len;
};

typedef i64 tag;

struct node {
	void *data;
	struct node *prev;
};

struct runzip_node {
	struct stream_info *sinfo;
	pthread_t *pthreads;
	struct runzip_node *prev;
};

struct rzip_state {
	void *ss;
	struct node *sslist;
	struct node *head;
	struct level *level;
	tag hash_index[256];
	struct hash_entry *hash_table;
	char hash_bits;
	i64 hash_count;
	i64 hash_limit;
	tag minimum_tag_mask;
	i64 tag_clean_ptr;
	i64 last_match;
	i64 chunk_size;
	i64 mmap_size;
	char chunk_bytes;
	uint32_t cksum;
	int fd_in, fd_out;
	char stdin_eof;
	struct {
		i64 inserts;
		i64 literals;
		i64 literal_bytes;
		i64 matches;
		i64 match_bytes;
		i64 tag_hits;
		i64 tag_misses;
	} stats;
};

/* lrzip library callback code removed */
struct rzip_control {
	char *locale;			// locale code
	char *infile;
	FILE *inFILE; 			// if a FILE is being read from
	char *outname;
	char *outfile;
	FILE *outFILE;			// if a FILE is being written to
	char *outdir;
	char *tmpdir;			// when stdin, stdout, or test used
	uchar *tmp_outbuf;		// Temporary file storage for stdout
	i64 out_ofs;			// Output offset when tmp_outbuf in use
	i64 hist_ofs;			// History offset
	i64 out_len;			// Total length of tmp_outbuf
	i64 out_maxlen;			// The largest the tmp_outbuf can be used
	i64 out_relofs;			// Relative tmp_outbuf offset when stdout has been flushed
	uchar *tmp_inbuf;
	i64 in_ofs;
	i64 in_len;
	i64 in_maxlen;
	FILE *msgout;			//stream for output messages
	FILE *msgerr;			//stream for output errors
	char *suffix;
	uchar compression_level;
	uchar rzip_compression_level;	// separate rzip compression level
	int comment_length;		// comment length
	uchar *comment;			// if there is a comment, 64 chars max
	i64 overhead;			// compressor overhead
	i64 usable_ram;			// the most ram we'll try to use on one activity
	i64 maxram;			// the largest chunk of ram to allocate
	unsigned filter_flag;		// flag for filters
	unsigned delta;			// delta flag offset (default 1)
	unsigned char lzma_properties[5];	// lzma properties, encoded
	u32 dictSize;			// lzma Dictionary size - set in overhead computation
	unsigned zpaq_level;		// zpaq level
	unsigned zpaq_bs;		// zpaq default block size
	unsigned bzip3_bs;		// bzip3 block size code (0-8)
	u32 bzip3_block_size;		// actual block size decoded
	i64 window;
	unsigned long flags;
	i64 ramsize;
	i64 max_chunk;
	i64 max_mmap;
	int threads;
	int threshold;			// threshold limit. 1-99%. Default no limiter
	char nice_val;			// added for consistency
	int current_priority;
	char major_version;
	char minor_version;
	i64 st_size;
	long page_size;
	int fd_in;
	int fd_out;
	int fd_hist;
	i64 encloops;
	i64 secs;

	/* encryption */
	uchar enc_code;			// encryption code from magic header or command line
	/* encryption key lengths will be determined at run time */
	char *enc_label;		// encryption label
	int *enc_gcode;			// gcrypt cioher code
	int *enc_keylen;		// gcrypt key length
	int *enc_ivlen;			// initialization vector length
	uchar salt[SALT_LEN];
	uchar *salt_pass;
	int salt_pass_len;
	uchar *hash;
	char *passphrase;

	/* hashes */
	uchar hash_code;		// hash code from magic header or command line
	/* hash lengths will be determined at runtime */
	char *hash_label;		// hash label
	int *hash_gcode;		// gcrypt hash code
	int *hash_len;			// hash length
	uchar *crc_code;		// CRC code. Will be 0
	char *crc_label;		// CRC label
	int *crc_gcode;			// gcrypt CRC code
	int *crc_len;			// CRC length
	cksem_t cksumsem;
	gcry_md_hd_t crc_handle;
	gcry_md_hd_t hash_handle;
	uchar *hash_resblock;		// block will have to be allocated at runtime
	i64 hash_read;			// How far into the file the hash has done so far

	pthread_mutex_t control_lock;
	unsigned char eof;
	unsigned char magic_written;
	bool lzma_prop_set;

	struct checksum checksum;

	const char *util_infile;
	char delete_infile;
	const char *util_outfile;
	char delete_outfile;
	FILE *outputfile;

	char chunk_bytes;
	struct sliding_buffer sb;
	void (*do_mcpy)(rzip_control *, unsigned char *, i64, i64);
	void (*next_tag)(rzip_control *, struct rzip_state *, i64, tag *);
	tag (*full_tag)(rzip_control *, struct rzip_state *, i64);
	i64 (*match_len)(rzip_control *, struct rzip_state *, i64, i64, i64, i64 *);

	pthread_t *pthreads;
	struct runzip_node *rulist;
	struct runzip_node *ruhead;
};

struct uncomp_thread {
	uchar *s_buf;
	i64 u_len, c_len;
	i64 last_head;
	uchar c_type;
	int busy;
	int streamno;
};

struct stream {
	i64 last_head;
	uchar *buf;
	i64 buflen;
	i64 bufp;
	uchar eos;
	long uthread_no;
	long unext_thread;
	long base_thread;
	int total_threads;
	i64 last_headofs;
};

struct stream_info {
	struct stream *s;
	uchar num_streams;
	int fd;
	i64 bufsize;
	i64 cur_pos;
	i64 initial_pos;
	i64 total_read;
	i64 ram_alloced;
	i64 size;
	struct uncomp_thread *ucthreads;
	long thread_no;
	long next_thread;
	int chunks;
	char chunk_bytes;
};

extern bool progress_flag ; // print newline when verbose and last print was progress indicator
static inline void print_stuff(const rzip_control *control, int level, unsigned int line, const char *file, const char *func, const char *format, ...)
{
	va_list ap;
	/* lrzip library callback code removed */
	if (control->msgout) {
		va_start(ap, format);
		vfprintf(control->msgout, format, ap);
		va_end(ap);
		fflush(control->msgout);
	}
}

static inline void print_err(const rzip_control *control, unsigned int line, const char *file, const char *func, const char *format, ...)
{
	va_list ap;
	/* lrzip library callback code removed */
	if (control->msgerr) {
		va_start(ap, format);
		vfprintf(control->msgerr, format, ap);
		va_end(ap);
		fflush(control->msgerr);
	}
}

#define print_stuff(level, ...) do {		\
	if (progress_flag && level != 2) {	\
		print_stuff(control,level, __LINE__, __FILE__, __func__, "\n");	\
		progress_flag = false;		\
	}					\
	print_stuff(control, level, __LINE__, __FILE__, __func__, __VA_ARGS__); \
} while (0)

#define print_output(...)	do {		\
	print_stuff(1, __VA_ARGS__);		\
} while (0)

#define print_progress(...)	do {		\
	if (SHOW_PROGRESS) {			\
		print_stuff(2, __VA_ARGS__);	\
		if (!progress_flag)		\
			progress_flag = true;	\
	}					\
} while (0)

#define print_verbose(...)	do {		\
	if (VERBOSE) {				\
		print_stuff(3, __VA_ARGS__);	\
	}					\
} while (0)

#define print_maxverbose(...)	do {		\
	if (MAX_VERBOSE) {			\
		print_stuff(4, __VA_ARGS__);	\
	}					\
} while (0)

#define print_err(...) do {\
	print_err(control, __LINE__, __FILE__, __func__, __VA_ARGS__); \
} while (0)
#endif
