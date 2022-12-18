/*
   Copyright (C) 2006-2016,2018, 2021 Con Kolivas
   Copyright (C) 2011 Serge Belyshev
   Copyright (C) 2011, 2019, 2020, 2021 Peter Hyman
   Copyright (C) 1998 Andrew Tridgell

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
/* multiplex N streams into a file - the streams are passed
   through different compressors */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
# include <sys/resource.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <sys/statvfs.h>
#include <pthread.h>
#include <bzlib.h>
#include <zlib.h>
#include <lzo/lzoconf.h>
#include <lzo/lzo1x.h>
#include <libbz3.h>
#include <lz4.h>
#ifdef HAVE_ERRNO_H
# include <errno.h>
#endif
#ifdef HAVE_ENDIAN_H
# include <endian.h>
#elif HAVE_SYS_ENDIAN_H
# include <sys/endian.h>
#endif
#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif

/* LZMA C Wrapper */
#include "LzmaLib.h"

#include "util.h"
#include "lrzip_core.h"
#include <math.h>

#include "Bra.h"	//Filters
#include "Delta.h"	//Delta Filter


/* This is not needed since it is defined in lrzip_private.h
 * #define STREAM_BUFSIZE (1024 * 1024 * 10)
 */

static struct compress_thread {
	uchar *s_buf;	/* Uncompressed buffer -> Compressed buffer */
	uchar c_type;	/* Compression type */
	i64 s_len;	/* Data length uncompressed */
	i64 c_len;	/* Data length compressed */
	cksem_t cksem;  /* This thread's semaphore */
	struct stream_info *sinfo;
	int streamno;
	uchar salt[SALT_LEN];
} *cthreads;

typedef struct stream_thread_struct {
	int i;		/* this is the current thread */
	rzip_control *control;
	struct stream_info *sinfo;
} stream_thread_struct;

static int output_thread;
static pthread_mutex_t output_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t output_cond = PTHREAD_COND_INITIALIZER;

static unsigned save_threads = 0;	// need for multiple chunks to restore thread count
static i64 limit = 0;			// save for open_stream_out
static i64 stream_bufsize = 0;		// save for open_stream_out

bool init_mutex(rzip_control *control, pthread_mutex_t *mutex)
{
	if (unlikely(pthread_mutex_init(mutex, NULL)))
		fatal("Failed to pthread_mutex_init\n");
	return true;
}

bool unlock_mutex(rzip_control *control, pthread_mutex_t *mutex)
{
	if (unlikely(pthread_mutex_unlock(mutex)))
		fatal("Failed to pthread_mutex_unlock\n");
	return true;
}

bool lock_mutex(rzip_control *control, pthread_mutex_t *mutex)
{
	if (unlikely(pthread_mutex_lock(mutex)))
		fatal("Failed to pthread_mutex_lock\n");
	return true;
}

static bool cond_wait(rzip_control *control, pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	if (unlikely(pthread_cond_wait(cond, mutex)))
		fatal("Failed to pthread_cond_wait\n");
	return true;
}

static bool cond_broadcast(rzip_control *control, pthread_cond_t *cond)
{
	if (unlikely(pthread_cond_broadcast(cond)))
		fatal("Failed to pthread_cond_broadcast\n");
	return true;
}

bool create_pthread(rzip_control *control, pthread_t *thread, pthread_attr_t * attr,
	void * (*start_routine)(void *), void *arg)
{
	if (unlikely(pthread_create(thread, attr, start_routine, arg)))
		fatal("Failed to pthread_create\n");
	return true;
}

bool detach_pthread(rzip_control *control, pthread_t *thread)
{
	if (unlikely(pthread_detach(*thread)))
		fatal("Failed to pthread_detach\n");
	return true;
}

bool join_pthread(rzip_control *control, pthread_t th, void **thread_return)
{
	if (pthread_join(th, thread_return))
		fatal("Failed to pthread_join\n");
	return true;
}

/* just to keep things clean, declare function here
 * but move body to the end since it's a work function
*/
static int lz4_compresses(rzip_control *control, uchar *s_buf, i64 s_len);

/*
  ***** COMPRESSION FUNCTIONS *****

  ZPAQ, BZIP, GZIP, LZMA, LZO, BZIP3

  try to compress a buffer. If compression fails for whatever reason then
  leave uncompressed. Return the compression type in c_type and resulting
  length in c_len
*/

static int bzip3_compress_buf(rzip_control *control, struct compress_thread *cthread, int current_thread)
{
	i64 c_len, c_size;
	uchar *c_buf;

	struct bz3_state *state;

	if (LZ4_TEST) {
		if (!lz4_compresses(control, cthread->s_buf, cthread->s_len))
			return 0;
	}

	c_size = round_up_page(control, cthread->s_len + cthread->s_len / 50 + 30);
	c_buf = malloc(c_size);
	if (!c_buf) {
		print_err("Unable to allocate c_buf in bzip3_compress_buf\n");
		return -1;
	}
	memcpy(c_buf, cthread->s_buf, cthread->s_len);

	c_len = 0;
	print_verbose("Starting bzip3: bs=%d - %'"PRIu32" bytes backend...\n",
		       control->bzip3_bs, control->bzip3_block_size);

	state = bz3_new(control->bzip3_block_size);	// allocate bzip3 state
	if (!state)
		fatal("Failed to allocate %'"PRIu32" bytes bzip3 state.\n", control->bzip3_block_size);

        c_len = bz3_encode_block(state, c_buf, cthread->s_len);

	if (unlikely(c_len >= cthread->c_len)) {
		print_maxverbose("Incompressible block\n");
		/* Incompressible, leave as CTYPE_NONE */
		dealloc(c_buf);
		return 0;
	}

	cthread->c_len = c_len;
	dealloc(cthread->s_buf);
	cthread->s_buf = c_buf;
	cthread->c_type = CTYPE_BZIP3;
	bz3_free(state);				// free bzip3 state
	return 0;
}

static int zpaq_compress_buf(rzip_control *control, struct compress_thread *cthread, int current_thread)
{
	i64 c_len, c_size;
	uchar *c_buf;
	int zpaq_redundancy, zpaq_type=0, compressibility;
	char method[10]; /* level, block size, redundancy of compression, type */

	/* if we're testing compressibility */
	if (LZ4_TEST) {
		if (!(compressibility=lz4_compresses(control, cthread->s_buf, cthread->s_len)))
			return 0;
	} /* else set compressibility to a neutral value */
	else
		compressibility = 50;	/* midpoint */


	c_size = round_up_page(control, cthread->s_len + 10000);
	c_buf = malloc(c_size);
	if (!c_buf) {
		print_err("Unable to allocate c_buf in zpaq_compress_buf\n");
		return -1;
	}

	c_len = 0;
        /* Compression level can be 1 to 5, zpaq version 7.15
	 * 1 and 2 faile however, so only levels 3-5 are used
	 * Data types are determined by zpaq_redundancy
	 * Type 0 = binary/random. Type 1 = text. Type 2 and 3 not used due to e8e9 */
	zpaq_redundancy = 256-(compressibility * 2.55);		/* 0, hard, 255, easy. Inverse of lz4_compresses */
	if (zpaq_redundancy < 25) zpaq_redundancy = 25;		/* too low a value fails */
	if (zpaq_redundancy > 192)
		zpaq_type = 1;					/* text data */

	sprintf(method,"%d%d,%d,%d",control->zpaq_level,control->zpaq_bs,zpaq_redundancy,zpaq_type);

	print_verbose("Starting zpaq backend compression thread %d...\nZPAQ: Method selected: %s: level=%d, bs=%d, redundancy=%d, type=%s\n",
		       current_thread, method, control->zpaq_level, control->zpaq_bs, zpaq_redundancy, (zpaq_type == 0 ? "binary/random" : "text"));

        zpaq_compress(c_buf, &c_len, cthread->s_buf, cthread->s_len, &method[0],
			control->msgout, SHOW_PROGRESS ? true: false, current_thread);

	if (unlikely(c_len >= cthread->c_len)) {
		print_maxverbose("Incompressible block\n");
		/* Incompressible, leave as CTYPE_NONE */
		dealloc(c_buf);
		return 0;
	}

	cthread->c_len = c_len;
	dealloc(cthread->s_buf);
	cthread->s_buf = c_buf;
	cthread->c_type = CTYPE_ZPAQ;
	return 0;
}

static int bzip2_compress_buf(rzip_control *control, struct compress_thread *cthread)
{
	u32 dlen = round_up_page(control, cthread->s_len);
	int bzip2_ret;
	uchar *c_buf;

	if (LZ4_TEST) {
		if (!lz4_compresses(control, cthread->s_buf, cthread->s_len))
			return 0;
	}

	c_buf = malloc(dlen);
	if (!c_buf) {
		print_err("Unable to allocate c_buf in bzip2_compress_buf\n");
		return -1;
	}

	bzip2_ret = BZ2_bzBuffToBuffCompress((char *)c_buf, &dlen,
		(char *)cthread->s_buf, cthread->s_len,
		control->compression_level, 0, control->compression_level * 10);

	/* if compressed data is bigger then original data leave as
	 * CTYPE_NONE */

	if (bzip2_ret == BZ_OUTBUFF_FULL) {
		print_maxverbose("Incompressible block\n");
		/* Incompressible, leave as CTYPE_NONE */
		dealloc(c_buf);
		return 0;
	}

	if (unlikely(bzip2_ret != BZ_OK)) {
		dealloc(c_buf);
		print_maxverbose("BZ2 compress failed\n");
		return -1;
	}

	if (unlikely(dlen >= cthread->c_len)) {
		print_maxverbose("Incompressible block\n");
		/* Incompressible, leave as CTYPE_NONE */
		dealloc(c_buf);
		return 0;
	}

	cthread->c_len = dlen;
	dealloc(cthread->s_buf);
	cthread->s_buf = c_buf;
	cthread->c_type = CTYPE_BZIP2;
	return 0;
}

static int gzip_compress_buf(rzip_control *control, struct compress_thread *cthread)
{
	unsigned long dlen = round_up_page(control, cthread->s_len);
	uchar *c_buf;
	int gzip_ret;

	c_buf = malloc(dlen);
	if (!c_buf) {
		print_err("Unable to allocate c_buf in gzip_compress_buf\n");
		return -1;
	}

	gzip_ret = compress2(c_buf, &dlen, cthread->s_buf, cthread->s_len,
		control->compression_level);

	/* if compressed data is bigger then original data leave as
	 * CTYPE_NONE */

	if (gzip_ret == Z_BUF_ERROR) {
		print_maxverbose("Incompressible block\n");
		/* Incompressible, leave as CTYPE_NONE */
		dealloc(c_buf);
		return 0;
	}

	if (unlikely(gzip_ret != Z_OK)) {
		dealloc(c_buf);
		print_maxverbose("compress2 failed\n");
		return -1;
	}

	if (unlikely((i64)dlen >= cthread->c_len)) {
		print_maxverbose("Incompressible block\n");
		/* Incompressible, leave as CTYPE_NONE */
		dealloc(c_buf);
		return 0;
	}

	cthread->c_len = dlen;
	dealloc(cthread->s_buf);
	cthread->s_buf = c_buf;
	cthread->c_type = CTYPE_GZIP;
	return 0;
}

static int lzma_compress_buf(rzip_control *control, struct compress_thread *cthread, int current_thread)
{
	unsigned char lzma_properties[5]; /* lzma properties, encoded */
	int lzma_level, lzma_ret;
	size_t prop_size = 5; /* return value for lzma_properties */
	uchar *c_buf;
	size_t dlen;

	if (LZ4_TEST) {
		if (!lz4_compresses(control, cthread->s_buf, cthread->s_len))
			return 0;
	}

	print_maxverbose("Starting lzma back end compression thread %'d...\n", current_thread);
retry:
	dlen = round_up_page(control, cthread->s_len * 1.02); // add 2% for lzma overhead to prevent memory overrun
	c_buf = malloc(dlen);
	if (!c_buf) {
		print_err("Unable to allocate c_buf in lzma_compress_buf\n");
		return -1;
	}
	/* pass absolute dictionary size and compression level */
	lzma_ret = LzmaCompress(c_buf, &dlen, cthread->s_buf,
		(size_t)cthread->s_len, lzma_properties, &prop_size,
				control->compression_level,
				control->dictSize, /* dict size. 0 = set default, otherwise control->dictSize */
				LZMA_LC, LZMA_LP, LZMA_PB, /* lc, lp, pb */
				(control->compression_level < 7 ? 32 : 64), /* fb */
				(control->threads > 1 ? 2 : 1));
				/* LZMA spec has threads = 1 or 2 only. */
	if (lzma_ret != SZ_OK) {
		switch (lzma_ret) {
			case SZ_ERROR_MEM:
				break;
			case SZ_ERROR_PARAM:
				print_err("LZMA Parameter ERROR: %'d. This should not happen.\n", SZ_ERROR_PARAM);
				break;
			case SZ_ERROR_OUTPUT_EOF:
				print_maxverbose("Harmless LZMA Output Buffer Overflow error: %'d. Incompressible block.\n", SZ_ERROR_OUTPUT_EOF);
				break;
			case SZ_ERROR_THREAD:
				print_err("LZMA Multi Thread ERROR: %'d. This should not happen.\n", SZ_ERROR_THREAD);
				break;
			default:
				print_err("Unidentified LZMA ERROR: %'d. This should not happen.\n", lzma_ret);
				break;
		}
		/* can pass -1 if not compressible! Thanks Lasse Collin */
		dealloc(c_buf);
		if (lzma_ret == SZ_ERROR_MEM) {
			if (lzma_level > 1) {
				lzma_level--;
				print_verbose("LZMA Warning: %'d. Can't allocate enough RAM for compression window, trying smaller.\n", SZ_ERROR_MEM);
				goto retry;
			}
			/* lzma compress can be fragile on 32 bit. If it fails,
			 * fall back to bzip2 compression so the block doesn't
			 * remain uncompressed */
			print_verbose("Unable to allocate enough RAM for any sized compression window, falling back to bzip2 compression.\n");
			return bzip2_compress_buf(control, cthread);
		} else if (lzma_ret == SZ_ERROR_OUTPUT_EOF)
			return 0;
		return -1;
	}

	if (unlikely((i64)dlen >= cthread->c_len)) {
		/* Incompressible, leave as CTYPE_NONE */
		print_maxverbose("Incompressible block\n");
		dealloc(c_buf);
		return 0;
	}

	/* Make sure multiple threads don't race on writing lzma_properties */
	lock_mutex(control, &control->control_lock);
	if (!control->lzma_prop_set) {
		memcpy(control->lzma_properties, lzma_properties, 5);
		control->lzma_prop_set = true;
		/* Reset the magic written flag so we write it again if we
		 * get lzma properties and haven't written them yet. */
		if (TMP_OUTBUF)
			control->magic_written = 0;
	}
	unlock_mutex(control, &control->control_lock);

	cthread->c_len = dlen;
	dealloc(cthread->s_buf);
	cthread->s_buf = c_buf;
	cthread->c_type = CTYPE_LZMA;
	return 0;
}

static int lzo_compress_buf(rzip_control *control, struct compress_thread *cthread)
{
	lzo_uint in_len = cthread->s_len;
	lzo_uint dlen = round_up_page(control, in_len + in_len / 16 + 64 + 3);
	lzo_bytep wrkmem;
	uchar *c_buf;
	int ret = -1;

	wrkmem = (lzo_bytep) calloc(1, LZO1X_1_MEM_COMPRESS);
	if (unlikely(wrkmem == NULL)) {
		print_maxverbose("Failed to malloc wkmem\n");
		return ret;
	}

	c_buf = malloc(dlen);
	if (!c_buf) {
		print_err("Unable to allocate c_buf in lzo_compress_buf");
		goto out_free;
	}

	/* lzo1x_1_compress does not return anything but LZO_OK so we ignore
	 * the return value */
	lzo1x_1_compress(cthread->s_buf, in_len, c_buf, &dlen, wrkmem);
	ret = 0;

	if (dlen >= in_len){
		/* Incompressible, leave as CTYPE_NONE */
		print_maxverbose("Incompressible block\n");
		dealloc(c_buf);
		goto out_free;
	}

	cthread->c_len = dlen;
	dealloc(cthread->s_buf);
	cthread->s_buf = c_buf;
	cthread->c_type = CTYPE_LZO;
out_free:
	dealloc(wrkmem);
	return ret;
}

/*
  ***** DECOMPRESSION FUNCTIONS *****

  ZPAQ, BZIP, GZIP, LZMA, LZO, BZIP3

  try to decompress a buffer. Return 0 on success and -1 on failure.
*/
static int bzip3_decompress_buf(rzip_control *control __UNUSED__, struct uncomp_thread *ucthread, int current_thread)
{
	i64 dlen = ucthread->u_len;
	uchar *c_buf;
	int ret = 0;

	struct bz3_state *state;

	c_buf = ucthread->s_buf;
	ucthread->s_buf = malloc(round_up_page(control, dlen));
	if (unlikely(!ucthread->s_buf)) {
		print_err("Failed to allocate %'"PRId64" bytes for decompression\n", dlen);
		ret = -1;
		goto out;
	}
	memcpy(ucthread->s_buf, c_buf, ucthread->c_len);

	state = bz3_new(control->bzip3_block_size);

	dlen = bz3_decode_block(state, ucthread->s_buf, ucthread->c_len, ucthread->u_len);
	if (bz3_last_error(state) != BZ3_OK)
		fatal("Failed to decompress with bz3 %s\n", bz3_strerror(state));

	if (unlikely(dlen != ucthread->u_len)) {
		print_err("Inconsistent length after decompression. Got %'"PRId64" bytes, expected %'"PRId64"\n", dlen, ucthread->u_len);
		ret = -1;
	} else
		dealloc(c_buf);
out:
	if (ret == -1) {
		dealloc(ucthread->s_buf);
		ucthread->s_buf = c_buf;
	}
	bz3_free(state);
	return ret;
}

static int zpaq_decompress_buf(rzip_control *control __UNUSED__, struct uncomp_thread *ucthread, int current_thread)
{
	i64 dlen = ucthread->u_len;
	uchar *c_buf;
	int ret = 0;

	c_buf = ucthread->s_buf;
	ucthread->s_buf = malloc(round_up_page(control, dlen));
	if (unlikely(!ucthread->s_buf)) {
		print_err("Failed to allocate %'"PRId64" bytes for decompression\n", dlen);
		ret = -1;
		goto out;
	}

	dlen = 0;
	zpaq_decompress(ucthread->s_buf, &dlen, c_buf, ucthread->c_len,
			control->msgout, SHOW_PROGRESS ? true: false, current_thread);

	if (unlikely(dlen != ucthread->u_len)) {
		print_err("Inconsistent length after decompression. Got %'"PRId64" bytes, expected %'"PRId64"\n", dlen, ucthread->u_len);
		ret = -1;
	} else
		dealloc(c_buf);
out:
	if (ret == -1) {
		dealloc(ucthread->s_buf);
		ucthread->s_buf = c_buf;
	}
	return ret;
}

static int bzip2_decompress_buf(rzip_control *control __UNUSED__, struct uncomp_thread *ucthread)
{
	u32 dlen = ucthread->u_len;
	int ret = 0, bzerr;
	uchar *c_buf;

	c_buf = ucthread->s_buf;
	ucthread->s_buf = malloc(round_up_page(control, dlen));
	if (unlikely(!ucthread->s_buf)) {
		print_err("Failed to allocate %'d bytes for decompression\n", dlen);
		ret = -1;
		goto out;
	}

	bzerr = BZ2_bzBuffToBuffDecompress((char*)ucthread->s_buf, &dlen, (char*)c_buf, ucthread->c_len, 0, 0);
	if (unlikely(bzerr != BZ_OK)) {
		print_err("Failed to decompress buffer - bzerr=%'d\n", bzerr);
		ret = -1;
		goto out;
	}

	if (unlikely(dlen != ucthread->u_len)) {
		print_err("Inconsistent length after decompression. Got %'d bytes, expected %'"PRId64"\n", dlen, ucthread->u_len);
		ret = -1;
	} else
		dealloc(c_buf);
out:
	if (ret == -1) {
		dealloc(ucthread->s_buf);
		ucthread->s_buf = c_buf;
	}
	return ret;
}

static int gzip_decompress_buf(rzip_control *control __UNUSED__, struct uncomp_thread *ucthread)
{
	unsigned long dlen = ucthread->u_len;
	int ret = 0, gzerr;
	uchar *c_buf;

	c_buf = ucthread->s_buf;
	ucthread->s_buf = malloc(round_up_page(control, dlen));
	if (unlikely(!ucthread->s_buf)) {
		print_err("Failed to allocate %'"PRId64" bytes for decompression\n", dlen);
		ret = -1;
		goto out;
	}

	gzerr = uncompress(ucthread->s_buf, &dlen, c_buf, ucthread->c_len);
	if (unlikely(gzerr != Z_OK)) {
		print_err("Failed to decompress buffer - gzerr=%'d\n", gzerr);
		ret = -1;
		goto out;
	}

	if (unlikely((i64)dlen != ucthread->u_len)) {
		print_err("Inconsistent length after decompression. Got %'"PRId64" bytes, expected %'"PRId64"\n", dlen, ucthread->u_len);
		ret = -1;
	} else
		dealloc(c_buf);
out:
	if (ret == -1) {
		dealloc(ucthread->s_buf);
		ucthread->s_buf = c_buf;
	}
	return ret;
}

static int lzma_decompress_buf(rzip_control *control, struct uncomp_thread *ucthread)
{
	size_t dlen = ucthread->u_len;
	int ret = 0, lzmaerr;
	uchar *c_buf;
	SizeT c_len = ucthread->c_len;

	c_buf = ucthread->s_buf;
	ucthread->s_buf = malloc(round_up_page(control, dlen));
	if (unlikely(!ucthread->s_buf)) {
		print_err("Failed to allocate %'"PRId64" bytes for decompression\n", (i64)dlen);
		ret = -1;
		goto out;
	}

	/* With LZMA SDK 4.63 we pass control->lzma_properties
	 * which is needed for proper uncompress */
	lzmaerr = LzmaUncompress(ucthread->s_buf, &dlen, c_buf, &c_len, control->lzma_properties, 5);
	if (unlikely(lzmaerr)) {
		print_err("Failed to decompress buffer - lzmaerr=%'d\n", lzmaerr);
		ret = -1;
		goto out;
	}

	if (unlikely((i64)dlen != ucthread->u_len)) {
		print_err("Inconsistent length after decompression. Got %'"PRId64" bytes, expected %'"PRId64"\n", (i64)dlen, ucthread->u_len);
		ret = -1;
	} else
		dealloc(c_buf);
out:
	if (ret == -1) {
		dealloc(ucthread->s_buf);
		ucthread->s_buf = c_buf;
	}
	return ret;
}

static int lzo_decompress_buf(rzip_control *control __UNUSED__, struct uncomp_thread *ucthread)
{
	lzo_uint dlen = ucthread->u_len;
	int ret = 0, lzerr;
	uchar *c_buf;

	c_buf = ucthread->s_buf;
	ucthread->s_buf = malloc(round_up_page(control, dlen));
	if (unlikely(!ucthread->s_buf)) {
		print_err("Failed to allocate %'"PRIu32" bytes for decompression\n", (unsigned long)dlen);
		ret = -1;
		goto out;
	}

	lzerr = lzo1x_decompress_safe((uchar*)c_buf, ucthread->c_len, (uchar*)ucthread->s_buf, &dlen, NULL);
	if (unlikely(lzerr != LZO_E_OK)) {
		print_err("Failed to decompress buffer - lzerr=%'d\n", lzerr);
		ret = -1;
		goto out;
	}

	if (unlikely((i64)dlen != ucthread->u_len)) {
		print_err("Inconsistent length after decompression. Got %'"PRIu32" bytes, expected %'"PRId64"\n", (unsigned long)dlen, ucthread->u_len);
		ret = -1;
	} else
		dealloc(c_buf);
out:
	if (ret == -1) {
		dealloc(ucthread->s_buf);
		ucthread->s_buf = c_buf;
	}
	return ret;
}

/* WORK FUNCTIONS */

/* Look at whether we're writing to a ram location or physical files and write
 * the data accordingly. */
ssize_t put_fdout(rzip_control *control, void *offset_buf, ssize_t ret)
{
	if (!TMP_OUTBUF)
		return write(control->fd_out, offset_buf, (size_t)ret);

	if (unlikely(control->out_ofs + ret > control->out_maxlen)) {
		/* The data won't fit in a temporary output buffer so we have
		 * to fall back to temporary files. */
		print_verbose("Unable to decompress entirely in ram, will use physical files\n");
		if (unlikely(control->fd_out == -1))
			fatal("Was unable to decompress entirely in ram and no temporary file creation was possible\n");
		if (unlikely(!write_fdout(control, control->tmp_outbuf, control->out_len))) {
			print_err("Unable to write_fdout tmpoutbuf in put_fdout\n");
			return -1;
		}
		close_tmpoutbuf(control);
		if (unlikely(!write_fdout(control, offset_buf, ret))) {
			print_err("Unable to write_fdout offset_buf in put_fdout\n");
			return -1;
		}
		return ret;
	}
	memcpy(control->tmp_outbuf + control->out_ofs, offset_buf, ret);
	control->out_ofs += ret;
	if (likely(control->out_ofs > control->out_len))
		control->out_len = control->out_ofs;
	return ret;
}

/* This is a custom version of write() which writes in 1GB chunks to avoid
   the overflows at the >= 2GB mark thanks to 32bit fuckage. */
ssize_t write_1g(rzip_control *control, void *buf, i64 len)
{
	uchar *offset_buf = buf;
	ssize_t ret;
	i64 total;

	total = 0;
	while (len > 0) {
		ret = len;
		ret = put_fdout(control, offset_buf, (size_t)ret);
		if (unlikely(ret <= 0))
			return ret;
		len -= ret;
		offset_buf += ret;
		total += ret;
	}
	return total;
}

/* Should be called only if we know the buffer will be large enough, otherwise
 * we must dump_stdin first */
static bool read_fdin(struct rzip_control *control, i64 len)
{
	int tmpchar;
	i64 i;

	for (i = 0; i < len; i++) {
		tmpchar = getchar();
		if (unlikely(tmpchar == EOF))
			fatal("Reached end of file on STDIN prematurely on read_fdin, asked for %'"PRId64" got %'"PRId64"\n", len, i);
		control->tmp_inbuf[control->in_ofs + i] = (char)tmpchar;
	}
	control->in_len = control->in_ofs + len;
	return true;
}

/* Dump STDIN into a temporary file */
static int dump_stdin(rzip_control *control)
{
	if (unlikely(!write_fdin(control)))
		return -1;
	if (unlikely(!read_tmpinfile(control, control->fd_in)))
		return -1;
	close_tmpinbuf(control);
	return 0;
}

/* Ditto for read */
ssize_t read_1g(rzip_control *control, int fd, void *buf, i64 len)
{
	uchar *offset_buf = buf;
	ssize_t ret;
	i64 total;

	if (TMP_INBUF && fd == control->fd_in) {
		/* We're decompressing from STDIN */
		if (unlikely(control->in_ofs + len > control->in_maxlen)) {
			/* We're unable to fit it all into the temp buffer */
			if (dump_stdin(control))
				fatal("Inadequate ram to %compress from STDIN and unable to create in tmpfile");
			goto read_fd;
		}
		if (control->in_ofs + len > control->in_len) {
			if (unlikely(!read_fdin(control, control->in_ofs + len - control->in_len)))
				return false;
		}
		memcpy(buf, control->tmp_inbuf + control->in_ofs, len);
		control->in_ofs += len;
		return len;
	}

	if (TMP_OUTBUF && fd == control->fd_out) {
		if (unlikely(control->out_ofs + len > control->out_maxlen))
			fatal("Trying to read beyond out_ofs in tmpoutbuf\n");
		memcpy(buf, control->tmp_outbuf + control->out_ofs, len);
		control->out_ofs += len;
		return len;
	}

read_fd:
	total = 0;
	while (len > 0) {
		ret = len;
		ret = read(fd, offset_buf, (size_t)ret);
		if (unlikely(ret <= 0))
			return ret;
		len -= ret;
		offset_buf += ret;
		total += ret;
	}
	return total;
}

/* write to a file, return 0 on success and -1 on failure */
static int write_buf(rzip_control *control, uchar *p, i64 len)
{
	ssize_t ret;

	ret = write_1g(control, p, (size_t)len);
	if (unlikely(ret == -1)) {
		print_err("Write of length %'"PRId64" failed - %s\n", len, strerror(errno));
		return -1;
	}
	if (unlikely(ret != (ssize_t)len)) {
		print_err("Partial write!? asked for %'"PRId64" bytes but got %'"PRId64"\n", len, (i64)ret);
		return -1;
	}
	return 0;
}

/* write a byte */
static inline int write_u8(rzip_control *control, uchar v)
{
	return write_buf(control, &v, 1);
}

static inline int write_val(rzip_control *control, i64 v, int len)
{
	v = htole64(v);
	return write_buf(control, (uchar *)&v, len);
}

static int read_buf(rzip_control *control, int f, uchar *p, i64 len)
{
	ssize_t ret;

	ret = read_1g(control, f, p, (size_t)len);
	if (unlikely(ret == -1)) {
		print_err("Read of length %'"PRId64" failed - %s\n", len, strerror(errno));
		return -1;
	}
	if (unlikely(ret != (ssize_t)len)) {
		print_err("Partial read!? asked for %'"PRId64" bytes but got %'"PRId64"\n", len, (i64)ret);
		return -1;
	}
	return 0;
}

static inline int read_u8(rzip_control *control, int f, uchar *v)
{
	return read_buf(control, f, v, 1);
}

static inline int read_u32(rzip_control *control, int f, u32 *v)
{
	int ret = read_buf(control, f, (uchar *)v, 4);

	*v = le32toh(*v);
	return ret;
}

static inline int read_val(rzip_control *control, int f, i64 *v, int len)
{
	int ret;

	/* We only partially read all 8 bytes so have to zero v here */
	*v = 0;
	ret = read_buf(control, f, (uchar *)v, len);
	return ret;
}

static int fd_seekto(rzip_control *control, struct stream_info *sinfo, i64 spos, i64 pos)
{
	if (unlikely(lseek(sinfo->fd, spos, SEEK_SET) != spos)) {
		print_err("Failed to seek to %'"PRId64" in stream\n", pos);
		return -1;
	}
	return 0;
}

/* seek to a position within a set of streams - return -1 on failure */
static int seekto(rzip_control *control, struct stream_info *sinfo, i64 pos)
{
	i64 spos = pos + sinfo->initial_pos;

	if (TMP_OUTBUF) {
		spos -= control->out_relofs;
		control->out_ofs = spos;
		if (unlikely(spos > control->out_len || spos < 0)) {
			print_err("Trying to seek to %'"PRId64" outside tmp outbuf in seekto\n", spos);
			return -1;
		}
		return 0;
	}

	return fd_seekto(control, sinfo, spos, pos);
}

static int read_seekto(rzip_control *control, struct stream_info *sinfo, i64 pos)
{
	i64 spos = pos + sinfo->initial_pos;

	if (TMP_INBUF) {
		if (spos > control->in_len) {
			i64 len = spos - control->in_len;

			if (control->in_ofs + len > control->in_maxlen) {
				if (unlikely(dump_stdin(control)))
					return -1;
				goto fd_seek;
			} else {
				if (unlikely(!read_fdin(control, len)))
					return -1;
			}
		}
		control->in_ofs = spos;
		if (unlikely(spos < 0)) {
			print_err("Trying to seek to %'"PRId64" outside tmp inbuf in read_seekto\n", spos);
			return -1;
		}
		return 0;
	}
fd_seek:
	return fd_seekto(control, sinfo, spos, pos);
}

static i64 get_seek(rzip_control *control, int fd)
{
	i64 ret;

	if (TMP_OUTBUF)
		return control->out_relofs + control->out_ofs;
	ret = lseek(fd, 0, SEEK_CUR);
	if (unlikely(ret == -1))
		fatal("Failed to lseek in get_seek\n");
	return ret;
}

i64 get_readseek(rzip_control *control, int fd)
{
	i64 ret;

	if (TMP_INBUF)
		return control->in_ofs;
	ret = lseek(fd, 0, SEEK_CUR);
	if (unlikely(ret == -1))
		fatal("Failed to lseek in get_seek\n");
	return ret;
}

bool prepare_streamout_threads(rzip_control *control)
{
	pthread_t *threads;
	int i;

	/* As we serialise the generation of threads during the rzip
	 * pre-processing stage, it's faster to have one more thread available
	 * to keep all CPUs busy. There is no point splitting up the chunks
	 * into multiple threads if there will be no compression back end. */
	if (control->threads > 1)
		++control->threads;
	if (NO_COMPRESS)
		control->threads = 1;
	threads = control->pthreads = calloc(sizeof(pthread_t), control->threads);
	if (unlikely(!threads))
		fatal("Unable to calloc threads in prepare_streamout_threads\n");

	cthreads = calloc(sizeof(struct compress_thread), control->threads);
	if (unlikely(!cthreads)) {
		dealloc(threads);
		fatal("Unable to calloc cthreads in prepare_streamout_threads\n");
	}

	for (i = 0; i < control->threads; i++) {
		cksem_init(control, &cthreads[i].cksem);
		cksem_post(control, &cthreads[i].cksem);
	}
	return true;
}


bool close_streamout_threads(rzip_control *control)
{
	int i, close_thread = output_thread;

	/* Wait for the threads in the correct order in case they end up
	 * serialised */
	for (i = 0; i < control->threads; i++) {
		cksem_wait(control, &cthreads[close_thread].cksem);

		if (++close_thread == control->threads)
			close_thread = 0;
	}
	dealloc(cthreads);
	dealloc(control->pthreads);
	return true;
}

/* open a set of output streams, compressing with the given
   compression level and algorithm */
void *open_stream_out(rzip_control *control, int f, unsigned int n, i64 chunk_limit, char cbytes)
{
	struct stream_info *sinfo;
	i64 testsize;	// limit made static to prevent recurring tests of memory;
	uchar *testmalloc;
	unsigned int i, testbufs;

	sinfo = calloc(sizeof(struct stream_info), 1);
	if (unlikely(!sinfo))
		return NULL;
	if (chunk_limit < control->page_size)
		chunk_limit = control->page_size;
	sinfo->bufsize = sinfo->size = chunk_limit;

	sinfo->chunk_bytes = cbytes;
	sinfo->num_streams = n;
	sinfo->fd = f;

	sinfo->s = calloc(sizeof(struct stream), n);
	if (unlikely(!sinfo->s)) {
		dealloc(sinfo);
		return NULL;
	}

	/* Find the largest we can make the window based on ability to malloc
	 * ram. We need 2 buffers for each compression thread and the overhead
	 * of each compression back end. No 2nd buf is required when there is
	 * no back end compression. We limit the total regardless to 1/3 ram
	 * for when the OS lies due to heavy overcommit. */
	testbufs = NO_COMPRESS ? 1 : 2;

	/* Reduce threads one by one and then reduce dictionary size
	* This block only has to be done once since memory never
	* changes, chunk sizes are the same. */

	if (save_threads == 0) {
		save_threads = control->threads;		// save threads for loops
		limit = control->usable_ram/testbufs;		// this is max chunk limit based on ram and compression window
		bool overhead_set = false;

		int thread_limit = control->threads >= PROCESSORS / 2  ? (control->threads / 2) : control->threads;	// control thread loop
		uchar exponent = lzma2_prop_from_dic(control->dictSize);	// lzma2 coding for dictsize spec
		if (LZMA_COMPRESS) {
			u32 save_dictSize = control->dictSize;	// save dictionary
			u32 DICTSIZEMIN = (1 << 24);		// minimum dictionary size (16MB)
			uchar save_exponent = exponent;		// save exponent
retry_lzma:
			do {
				for (control->threads = save_threads; control->threads >= thread_limit; control->threads--) {
					if (limit >= (control->overhead * control->threads / testbufs)) {
						overhead_set = true;			// got it!
						break;
					}
				}	// thread loop
				if (overhead_set == true)
					break;
				else {
					// reduce dictionary size
					exponent -= 1;					// exponent does not decrememt properly in macro
					control->dictSize = LZMA2_DIC_SIZE_FROM_PROP(exponent);
				}
				setup_overhead(control);				// recompute overhead
			} while (control->dictSize > DICTSIZEMIN);			// dictionary size loop
			if (!overhead_set && thread_limit > 1) {			// try again and lower thread_limit
				thread_limit--;
				control->dictSize = save_dictSize;			// restore dictionary
				exponent = save_exponent;				// restore exponent
				goto retry_lzma;
			}
			if (control->dictSize != save_dictSize)
				print_verbose("Dictionary Size reduced to %'"PRIu32"\n", control->dictSize);
		} else if (ZPAQ_COMPRESS) {
			/* compute max possible block size */
			int save_bs = control->zpaq_bs;
			int ZPAQBSMIN = 4;
retry_zpaq:
			do {
				for (control->threads = save_threads; control->threads >= thread_limit; control->threads--) {
					if (limit >= control->overhead * control->threads) {
						overhead_set = true;
						break;
					}
				} // thread loop
				if (overhead_set == true)
					break;
				else
					control->zpaq_bs--;				// decrement block size
				setup_overhead(control);				// recompute overhead
			} while (control->zpaq_bs > ZPAQBSMIN);				// block size loop
			if (!overhead_set && thread_limit > 1) {			// try again and lower thread_limit
				thread_limit--;
				control->zpaq_bs = save_bs;				// restore block size
				goto retry_zpaq;
			}
			if (control->zpaq_bs != save_bs)
				print_verbose("ZPAQ Block Size reduced to %d\n", control->zpaq_bs);
		} else if(BZIP3_COMPRESS) {
			/* compute max possible block size. NB: This code sucks but I don't want to refactor it. */
			int save_bs = control->bzip3_bs;
			u32 BZIP3BSMIN = (1 << 25);
retry_bzip3:
			do {
				for (control->threads = save_threads; control->threads >= thread_limit; control->threads--) {
					if (limit >= control->overhead * control->threads / testbufs) {
						overhead_set = true;
						break;
					}
				} // thread loop
				if (overhead_set == true)
					break;
				else
					control->bzip3_bs--;				// decrement block size
					control->bzip3_block_size = BZIP3_BLOCK_SIZE_FROM_PROP(control->bzip3_bs);

				setup_overhead(control);				// recompute overhead
			} while (control->bzip3_block_size > BZIP3BSMIN);				// block size loop
			if (!overhead_set && thread_limit > 1) {			// try again and lower thread_limit
				thread_limit--;
				control->bzip3_bs = save_bs;				// restore block size
				goto retry_bzip3;
			}
			if (control->bzip3_bs != save_bs)
				print_verbose("BZIP3 Block Size reduced to %d - %'"PRIu32"\n", control->bzip3_bs, control->bzip3_block_size);
		}

		if (control->threads != save_threads)
			print_verbose("Threads reduced to %'d\n", control->threads);

		print_verbose("Per Thread Memory Overhead is %'"PRId64"\n", control->overhead);

		save_threads = control->threads;			// preserve thread count. Important

		/* Bye bye 32 bit */
		/* check control->st_size. If compressing a file, check the following:
		 * 1. if control->st_size > limit, make sure limit not > than chunk_limit
		 * 2. if control->st_size = 0, then same test as 1
		 * 3. if control->st_size > 0 and < limit, then limit = control->st_size or STREAM_BUFSIZE if really small file
		 * control->sb.orig_size has buffering info, but only for rzip pre-processing
		 * This will hopefully force all threads to be used based on computed overhead
		 */
		if (control->st_size > 0 && control->st_size < limit)
			limit = MAX(control->st_size, STREAM_BUFSIZE);
		/* test limit against chunk_limit */
		else if (limit > chunk_limit)
			limit = chunk_limit;
retest_malloc:
		testsize = limit + (control->overhead * control->threads);
		testmalloc = malloc(testsize);
		if (!testmalloc) {
			limit = limit / 10 * 9;
			if (limit < 100000000) {
				/* If we can't even allocate 100MB then we'll never
				 * succeed */
				print_err("Unable to allocate enough memory for operation\n");
				dealloc(sinfo->s);
				dealloc(sinfo);
				return NULL;
			}
			goto retest_malloc;
		}
		dealloc(testmalloc);
		print_maxverbose("Succeeded in testing %'"PRId64" sized malloc for back end compression\n", testsize);
		/* Make the bufsize no smaller than STREAM_BUFSIZE. Round up the
		 * bufsize to fit X threads into it
		 * For ZPAQ the limit is 2^bs * 1MB */
		if (ZPAQ_COMPRESS && (limit/control->threads > 0x100000<<control->zpaq_bs))
			// ZPAQ  buffer always larger than STREAM_BUFSIZE
			stream_bufsize = round_up_page(control, (0x100000<<control->zpaq_bs)-0x1000);
		else if (BZIP3_COMPRESS && (limit/control->threads > control->bzip3_block_size))
			stream_bufsize = round_up_page(control, control->bzip3_block_size-0x1000);
		else if (LZMA_COMPRESS && limit/control->threads > STREAM_BUFSIZE)
			// for smaller dictionary sizes, need MAX test to bring in larger buffer from limit
			// limit = usable ram / 2
			// buffer size will be larger of limit/threads OR (overhead-dictsize)/threads
			// we want buffer to be smaller than overhead but still large in cases of small dictsize and overhead
			stream_bufsize = round_up_page(control, MAX(limit, control->overhead-control->dictSize)/control->threads);
		else	// all other methods
			stream_bufsize = round_up_page(control, MIN(limit, MAX(limit/control->threads, STREAM_BUFSIZE)));

		if (control->threads > 1)
			print_maxverbose("Using up to %'d threads to compress up to %'"PRId64" bytes each.\n",
				control->threads, stream_bufsize);
		else
			print_maxverbose("Using only 1 thread to compress up to %'"PRId64" bytes\n",
				stream_bufsize);
	} // end -- determine limit

	control->threads = save_threads;				// restore threads. This is important!

	sinfo->bufsize = stream_bufsize;

	for (i = 0; i < n; i++) {
		sinfo->s[i].buf = calloc(sinfo->bufsize , 1);
		if (unlikely(!sinfo->s[i].buf)) {
			fatal("Unable to malloc buffer of size %'"PRId64" in open_stream_out\n", sinfo->bufsize);
			dealloc(sinfo->s);
			dealloc(sinfo);
			return NULL;
		}
	}

	return (void *)sinfo;
}


/* prepare a set of n streams for reading on file descriptor f */
void *open_stream_in(rzip_control *control, int f, int n, char chunk_bytes)
{
	struct uncomp_thread *ucthreads;
	struct stream_info *sinfo;
	int total_threads, i;
	pthread_t *threads;
	i64 header_length;

	sinfo = calloc(sizeof(struct stream_info), 1);
	if (unlikely(!sinfo))
		return NULL;

	/* We have one thread dedicated to stream 0, and one more thread than
	 * CPUs to keep them busy, unless we're running single-threaded. */
	if (control->threads > 1)
		total_threads = control->threads + 2;
	else
		total_threads = control->threads + 1;
	threads = control->pthreads = calloc(sizeof(pthread_t), total_threads);
	if (unlikely(!threads))
		return NULL;

	sinfo->ucthreads = ucthreads = calloc(sizeof(struct uncomp_thread), total_threads);
	if (unlikely(!ucthreads)) {
		dealloc(sinfo);
		dealloc(threads);
		fatal("Unable to calloc ucthreads in open_stream_in\n");
	}

	sinfo->num_streams = n;
	sinfo->fd = f;
	sinfo->chunk_bytes = chunk_bytes;

	sinfo->s = calloc(sizeof(struct stream), n);
	if (unlikely(!sinfo->s)) {
		dealloc(sinfo);
		dealloc(threads);
		dealloc(ucthreads);
		return NULL;
	}

	sinfo->s[0].total_threads = 1;
	sinfo->s[1].total_threads = total_threads - 1;

	/* remove checks for lrzip < 0.6 */
	if (control->major_version == 0) {
		/* Read in flag that tells us if there are more chunks after
		 * this. Ignored if we know the final file size */
		print_maxverbose("Reading eof flag at %'"PRId64"\n", get_readseek(control, f));
		if (unlikely(read_u8(control, f, &control->eof))) {
			print_err("Failed to read eof flag in open_stream_in\n");
			goto failed;
		}
		print_maxverbose("EOF: %'d\n", control->eof);

		/* Read in the expected chunk size */
		if (!ENCRYPT) {
			print_maxverbose("Reading expected chunksize at %'"PRId64"\n", get_readseek(control, f));
			if (unlikely(read_val(control, f, &sinfo->size, sinfo->chunk_bytes))) {
				print_err("Failed to read in chunk size in open_stream_in\n");
				goto failed;
			}
			sinfo->size = le64toh(sinfo->size);
			if (sinfo->size)
				print_maxverbose("Chunk size: %'"PRId64"\n", sinfo->size);
			else
				/* FIXME need this to not show 0 bytes for chunk size */
				print_maxverbose("Chunk size: smaller than 4,096 bytes\n");
			control->st_size += sinfo->size;
			if (unlikely(sinfo->chunk_bytes < 1 || sinfo->chunk_bytes > 8 || sinfo->size < 0)) {
				print_err("Invalid chunk data size %'"PRId64" bytes %d\n", sinfo->size, sinfo->chunk_bytes);
				goto failed;
			}
		}
	}
	sinfo->initial_pos = get_readseek(control, f);
	if (unlikely(sinfo->initial_pos == -1))
		goto failed;

	for (i = 0; i < n; i++) {
		uchar c, enc_head[25 + SALT_LEN];
		i64 v1, v2;

		sinfo->s[i].base_thread = i;
		sinfo->s[i].uthread_no = sinfo->s[i].base_thread;
		sinfo->s[i].unext_thread = sinfo->s[i].base_thread;

		if (unlikely(ENCRYPT && read_buf(control, f, enc_head, SALT_LEN)))
			goto failed;
again:
		if (unlikely(read_u8(control, f, &c)))
			goto failed;

		/* remove checks for lrzip < 0.6 */
		if (control->major_version == 0) {
			int read_len;

			print_maxverbose("Reading stream %'d header at %'"PRId64"\n", i, get_readseek(control, f));
			if (ENCRYPT)
				read_len = 8;
			else
				read_len = sinfo->chunk_bytes;
			if (unlikely(read_val(control, f, &v1, read_len)))
				goto failed;
			if (unlikely(read_val(control, f, &v2, read_len)))
				goto failed;
			if (unlikely(read_val(control, f, &sinfo->s[i].last_head, read_len)))
				goto failed;
			header_length = 1 + (read_len * 3);
		}
		sinfo->total_read += header_length;

		if (ENCRYPT) {
			// pass decrypt flag instead of validate flag
			if (unlikely(!decrypt_header(control, enc_head, &c, &v1, &v2, &sinfo->s[i].last_head, LRZ_DECRYPT)))
				goto failed;
			sinfo->total_read += SALT_LEN;
		}

		v1 = le64toh(v1);
		v2 = le64toh(v2);
		sinfo->s[i].last_head = le64toh(sinfo->s[i].last_head);

		if (unlikely(c == CTYPE_NONE && v1 == 0 && v2 == 0 && sinfo->s[i].last_head == 0 && i == 0)) {
			print_err("Enabling stream close workaround\n");
			sinfo->initial_pos += header_length;
			goto again;
		}

		if (unlikely(c != CTYPE_NONE)) {
			print_err("Unexpected initial tag %'d in streams\n", c);
			if (ENCRYPT)
				print_err("Wrong password?\n");
			goto failed;
		}
		if (unlikely(v1)) {
			print_err("Unexpected initial c_len %'"PRId64" in streams %'"PRId64"\n", v1, v2);
			goto failed;
		}
		if (unlikely(v2)) {
			print_err("Unexpected initial u_len %'"PRId64" in streams\n", v2);
			goto failed;
		}
	}

	return (void *)sinfo;

failed:
	dealloc(sinfo->s);
	dealloc(sinfo);
	dealloc(threads);
	dealloc(ucthreads);
	return NULL;
}

/* Once the final data has all been written to the block header, we go back
 * and write SALT_LEN bytes of salt before it, and encrypt the header in place
 * by reading what has been written, encrypting it, and writing back over it.
 * This is very convoluted depending on whether a last_head value is written
 * to this block or not. See the callers of this function */
static bool rewrite_encrypted(rzip_control *control, struct stream_info *sinfo, i64 ofs)
{
	uchar *buf, *head;
	i64 cur_ofs;

	cur_ofs = get_seek(control, sinfo->fd) - sinfo->initial_pos;
	if (unlikely(cur_ofs == -1))
		return false;
	head = malloc(25 + SALT_LEN);
	if (unlikely(!head))
		fatal("Failed to malloc head in rewrite_encrypted\n");
	buf = head + SALT_LEN;
	gcry_create_nonce(head, SALT_LEN);
	if (unlikely(seekto(control, sinfo, ofs - SALT_LEN)))
		fatal("Failed to seekto buf ofs in rewrite_encrypted\n");
	if (unlikely(write_buf(control, head, SALT_LEN)))
		fatal("Failed to write_buf head in rewrite_encrypted\n");
	if (unlikely(read_buf(control, sinfo->fd, buf, 25)))
		fatal("Failed to read_buf buf in rewrite_encrypted\n");

	if (unlikely(!lrz_encrypt(control, buf, 25, head)))
		goto error;

	if (unlikely(seekto(control, sinfo, ofs)))
		fatal("Failed to seek back to ofs in rewrite_encrypted\n");
	if (unlikely(write_buf(control, buf, 25)))
		fatal("Failed to write_buf encrypted buf in rewrite_encrypted\n");
	dealloc(head);
	seekto(control, sinfo, cur_ofs);
	return true;
error:
	dealloc(head);
	return false;
}

/* Enter with s_buf allocated,s_buf points to the compressed data after the
 * backend compression and is then freed here */
static void *compthread(void *data)
{
	stream_thread_struct *s = data;
	rzip_control *control = s->control;
	int current_thread = s->i;
	struct compress_thread *cti;
	struct stream_info *ctis;
	int waited = 0, ret = 0;
	i64 padded_len;
	int write_len;

	/* Make sure this thread doesn't already exist */

	dealloc(data);
	cti = &cthreads[current_thread];
	ctis = cti->sinfo;

	if (unlikely(setpriority(PRIO_PROCESS, 0, control->nice_val) == -1)) {
		print_err("Warning, unable to set thread nice value %'d...Resetting to %'d\n", control->nice_val, control->current_priority);
		setpriority(PRIO_PROCESS, 0, (control->nice_val=control->current_priority));
	}
	cti->c_type = CTYPE_NONE;
	cti->c_len = cti->s_len;

	/* Flushing writes to disk frees up any dirty ram, improving chances
	 * of succeeding in allocating more ram */
	fsync(ctis->fd);

	/* This is a cludge in case we are compressing to stdout and our first
	 * stream is not compressed, but subsequent ones are compressed by
	 * lzma and we can no longer seek back to the beginning of the file
	 * to write the lzma properties which are effectively always starting
	 * with 93.= 0x5D. lc=3, lp=0, pb=2 */
	if (TMP_OUTBUF && LZMA_COMPRESS)
		control->lzma_properties[0] = LZMA_LC_LP_PB;
retry:
	/* Filters are used ragrdless of compression type */
	if (FILTER_USED && cti->streamno == 1) {	// stream 0 is for matches, stream 1+ is for literals
		print_maxverbose("Using %s filter prior to compression for thread %'d...\n",
				((control->filter_flag == FILTER_FLAG_X86) ? "x86" :
				((control->filter_flag == FILTER_FLAG_ARM) ? "ARM" :
				((control->filter_flag == FILTER_FLAG_ARMT) ? "ARMT" :
				((control->filter_flag == FILTER_FLAG_PPC) ? "PPC" :
				((control->filter_flag == FILTER_FLAG_SPARC) ? "SPARC" :
				((control->filter_flag == FILTER_FLAG_IA64) ? "IA64" :
				((control->filter_flag == FILTER_FLAG_DELTA) ? "Delta" : "wtf"))))))), current_thread);
		if (control->filter_flag == FILTER_FLAG_X86) {
			UInt32 x86State;
			x86_Convert_Init(x86State);
			x86_Convert(cti->s_buf, cti->s_len, 0, &x86State, 1);
		}
		else if (control->filter_flag == FILTER_FLAG_ARM) {
			ARM_Convert(cti->s_buf, cti->s_len, 0, 1);
		}
		else if (control->filter_flag == FILTER_FLAG_ARMT) {
			ARMT_Convert(cti->s_buf, cti->s_len, 0, 1);
		}
		else if (control->filter_flag == FILTER_FLAG_PPC) {
			PPC_Convert(cti->s_buf, cti->s_len, 0, 1);
		}
		else if (control->filter_flag == FILTER_FLAG_SPARC) {
			SPARC_Convert(cti->s_buf, cti->s_len, 0, 1);
		}
		if (control->filter_flag == FILTER_FLAG_IA64) {
			IA64_Convert(cti->s_buf, cti->s_len, 0, 1);
		}
		if (control->filter_flag == FILTER_FLAG_DELTA) {
			uchar delta_state[DELTA_STATE_SIZE];
			Delta_Init(delta_state);
			Delta_Encode(delta_state, control->delta, cti->s_buf,  cti->s_len);
		}
	}
	/* Very small buffers have issues to do with minimum amounts of ram
	 * allocatable to a buffer combined with the MINIMUM_MATCH of rzip
	 * being 31 bytes so don't bother trying to compress anything less
	 * than 64 bytes. */
	if (!NO_COMPRESS && cti->c_len >= 64) {
		/* Any Filter */
		if (LZMA_COMPRESS)
			ret = lzma_compress_buf(control, cti, current_thread);
		else if (LZO_COMPRESS)
			ret = lzo_compress_buf(control, cti);
		else if (BZIP2_COMPRESS)
			ret = bzip2_compress_buf(control, cti);
		else if (ZLIB_COMPRESS)
			ret = gzip_compress_buf(control, cti);
		else if (ZPAQ_COMPRESS)
			ret = zpaq_compress_buf(control, cti, current_thread);
		else if (BZIP3_COMPRESS)
			ret = bzip3_compress_buf(control, cti, current_thread);
		else fatal("Dunno wtf compression to use!\n");
	}

	padded_len = cti->c_len;
	if (!ret) {
		if (ENCRYPT) {
		/* We need to pad out each block to at least be CBC_LEN bytes
		 * long or encryption cannot work. We pad it with random
		 * data */
			if (padded_len < *control->enc_keylen)
				padded_len = *control->enc_keylen;
			cti->s_buf = realloc(cti->s_buf, padded_len);
			if (unlikely(!cti->s_buf))
				fatal("Failed to realloc s_buf in compthread\n");
			gcry_create_nonce(cti->s_buf + cti->c_len, padded_len - cti->c_len);
		}
	}

	/* If compression fails for whatever reason multithreaded, then wait
	 * for the previous thread to finish, serialising the work to decrease
	 * the memory requirements, increasing the chance of success */
	if (unlikely(ret && waited))
		fatal("Failed to compress in compthread\n");

	if (!waited) {
		lock_mutex(control, &output_lock);
		while (output_thread != current_thread)
			cond_wait(control, &output_cond, &output_lock);
		unlock_mutex(control, &output_lock);
		waited = 1;
	}
	if (unlikely(ret)) {
		print_maxverbose("Unable to compress in parallel, waiting for previous thread to complete before trying again\n");
		if (FILTER_USED && cti->streamno == 1 ) {	// As unlikely as this is, we have to undo filtering here
			print_maxverbose("Reverting filtering...\n");
			if (control->filter_flag == FILTER_FLAG_X86) {
				UInt32 x86State;
				x86_Convert_Init(x86State);
				x86_Convert(cti->s_buf, cti->s_len, 0, &x86State, 0);
			}
			else if (control->filter_flag == FILTER_FLAG_ARM) {
				ARM_Convert(cti->s_buf, cti->s_len, 0, 0);
			}
			else if (control->filter_flag == FILTER_FLAG_ARMT) {
				ARMT_Convert(cti->s_buf, cti->s_len, 0, 0);
			}
			else if (control->filter_flag == FILTER_FLAG_PPC) {
				PPC_Convert(cti->s_buf, cti->s_len, 0, 0);
			}
			else if (control->filter_flag == FILTER_FLAG_SPARC) {
				SPARC_Convert(cti->s_buf, cti->s_len, 0, 0);
			}
			else if (control->filter_flag == FILTER_FLAG_IA64) {
				IA64_Convert(cti->s_buf, cti->s_len, 0, 0);
			}
			else if (control->filter_flag == FILTER_FLAG_DELTA) {
				uchar delta_state[DELTA_STATE_SIZE];
				print_maxverbose("Reverting Delta filter data prior to trying again...\n");
				Delta_Init(delta_state);
				Delta_Decode(delta_state, control->delta, cti->s_buf,  cti->s_len);
			}
		}
		goto retry;
	}

	/* Need to be big enough to fill one CBC_LEN */
	if (ENCRYPT)
		write_len = 8;
	else
		write_len = ctis->chunk_bytes;

	if (!ctis->chunks++) {
		int j;

		if (TMP_OUTBUF) {
			lock_mutex(control, &control->control_lock);
			if (!control->magic_written)
				write_magic(control);
			unlock_mutex(control, &control->control_lock);

			if (unlikely(!flush_tmpoutbuf(control))) {
				print_err("Failed to flush_tmpoutbuf in compthread\n");
				goto error;
			}
		}

		print_maxverbose("Writing initial chunk bytes value %'d at %'"PRId64"\n",
				 ctis->chunk_bytes, get_seek(control, ctis->fd));
		/* Write chunk bytes of this block */
		write_u8(control, ctis->chunk_bytes);

		/* Write whether this is the last chunk, followed by the size
		 * of this chunk */
		print_maxverbose("Writing EOF flag as %'d\n", control->eof);
		write_u8(control, control->eof);
		if (!ENCRYPT)
			write_val(control, ctis->size, ctis->chunk_bytes);

		/* First chunk of this stream, write headers */
		ctis->initial_pos = get_seek(control, ctis->fd);
		if (unlikely(ctis->initial_pos == -1))
			goto error;

		print_maxverbose("Writing initial header at %'"PRId64"\n", ctis->initial_pos);
		for (j = 0; j < ctis->num_streams; j++) {
			/* If encrypting, we leave SALT_LEN room to write in salt
			* later */
			if (ENCRYPT) {
				if (unlikely(write_val(control, 0, SALT_LEN)))
					fatal("Failed to write_buf blank salt in compthread %'d\n", current_thread);
				ctis->cur_pos += SALT_LEN;
			}
			ctis->s[j].last_head = ctis->cur_pos + 1 + (write_len * 2);
			write_u8(control, CTYPE_NONE);
			write_val(control, 0, write_len);
			write_val(control, 0, write_len);
			write_val(control, 0, write_len);
			ctis->cur_pos += 1 + (write_len * 3);
		}
	}

	print_maxverbose("Compthread %'d seeking to %'"PRId64" to store length %'d\n", current_thread, ctis->s[cti->streamno].last_head, write_len);

	if (unlikely(seekto(control, ctis, ctis->s[cti->streamno].last_head)))
		fatal("Failed to seekto in compthread %'d\n", current_thread);

	if (unlikely(write_val(control, ctis->cur_pos, write_len)))
		fatal("Failed to write_val cur_pos in compthread %'d\n", current_thread);

	if (ENCRYPT)
		rewrite_encrypted(control, ctis, ctis->s[cti->streamno].last_head - 17);

	ctis->s[cti->streamno].last_head = ctis->cur_pos + 1 + (write_len * 2) + (ENCRYPT ? SALT_LEN : 0);

	print_maxverbose("Compthread %'d seeking to %'"PRId64" to write header\n", current_thread, ctis->cur_pos);

	if (unlikely(seekto(control, ctis, ctis->cur_pos)))
		fatal("Failed to seekto cur_pos in compthread %'d\n", current_thread);

	print_maxverbose("Thread %'d writing %'"PRId64" compressed bytes from stream %'d\n", current_thread, padded_len, cti->streamno);

	if (ENCRYPT) {
		if (unlikely(write_val(control, 0, SALT_LEN)))
			fatal("Failed to write_buf header salt in compthread %'d\n", current_thread);
		ctis->cur_pos += SALT_LEN;
		ctis->s[cti->streamno].last_headofs = ctis->cur_pos;
	}
	/* We store the actual c_len even though we might pad it out */
	if (unlikely(write_u8(control, cti->c_type) ||
		write_val(control, cti->c_len, write_len) ||
		write_val(control, cti->s_len, write_len) ||
		write_val(control, 0, write_len))) {
			fatal("Failed write in compthread %'d\n", current_thread);
	}
	ctis->cur_pos += 1 + (write_len * 3);

	if (ENCRYPT) {
		gcry_create_nonce(cti->salt, SALT_LEN);
		if (unlikely(write_buf(control, cti->salt, SALT_LEN)))
			fatal("Failed to write_buf block salt in compthread %'d\n", current_thread);
		if (unlikely(!lrz_encrypt(control, cti->s_buf, padded_len, cti->salt)))
			goto error;
		ctis->cur_pos += SALT_LEN;
	}

	print_maxverbose("Compthread %'d writing data at %'"PRId64"\n", current_thread, ctis->cur_pos);

	if (unlikely(write_buf(control, cti->s_buf, padded_len)))
		fatal("Failed to write_buf s_buf in compthread %'d\n", current_thread);

	ctis->cur_pos += padded_len;
	dealloc(cti->s_buf);

	lock_mutex(control, &output_lock);
	if (++output_thread == control->threads)
		output_thread = 0;
	cond_broadcast(control, &output_cond);
	unlock_mutex(control, &output_lock);

error:
	cksem_post(control, &cti->cksem);

	return NULL;
}

static void clear_buffer(rzip_control *control, struct stream_info *sinfo, int streamno, int newbuf)
{
	pthread_t *threads = control->pthreads;
	stream_thread_struct *s;
	static int current_thread = 0;

	/* Make sure this thread doesn't already exist */
	cksem_wait(control, &cthreads[current_thread].cksem);

	cthreads[current_thread].sinfo = sinfo;
	cthreads[current_thread].streamno = streamno;
	cthreads[current_thread].s_buf = sinfo->s[streamno].buf;
	cthreads[current_thread].s_len = sinfo->s[streamno].buflen;

	print_maxverbose("Starting thread %'d to compress %'"PRId64" bytes from stream %'d\n",
			 current_thread, cthreads[current_thread].s_len, streamno);

	s = malloc(sizeof(stream_thread_struct));
	if (unlikely(!s)) {
		cksem_post(control, &cthreads[current_thread].cksem);
		fatal("Unable to malloc in clear_buffer");
	}
	s->i = current_thread;
	s->control = control;
	if (unlikely((!create_pthread(control, &threads[current_thread], NULL, compthread, s)) ||
	             (!detach_pthread(control, &threads[current_thread]))))
		fatal("Unable to create compthread in clear_buffer");

	if (newbuf) {
		/* The stream buffer has been given to the thread, allocate a
		 * new one. */
		sinfo->s[streamno].buf = malloc(sinfo->bufsize);
		if (unlikely(!sinfo->s[streamno].buf))
			fatal("Unable to malloc buffer of size %'"PRId64" in flush_buffer\n", sinfo->bufsize);
		sinfo->s[streamno].buflen = 0;
	}

	if (++current_thread == control->threads)
		current_thread = 0;
}

/* flush out any data in a stream buffer */
void flush_buffer(rzip_control *control, struct stream_info *sinfo, int streamno)
{
	clear_buffer(control, sinfo, streamno, 1);
}

static void *ucompthread(void *data)
{
	stream_thread_struct *sts = data;
	rzip_control *control = sts->control;
	int waited = 0, ret = 0, current_thread = sts->i;
	struct uncomp_thread *uci = &sts->sinfo->ucthreads[current_thread];

	dealloc(data);

	if (unlikely(setpriority(PRIO_PROCESS, 0, control->nice_val) == -1)) {
		print_err("Warning, unable to set thread nice value %'d...Resetting to %'d\n", control->nice_val, control->current_priority);
		setpriority(PRIO_PROCESS, 0, (control->nice_val=control->current_priority));
	}

retry:
	if (uci->c_type != CTYPE_NONE) {
		switch (uci->c_type) {
			case CTYPE_LZMA:
				ret = lzma_decompress_buf(control, uci);
				break;
			case CTYPE_LZO:
				ret = lzo_decompress_buf(control, uci);
				break;
			case CTYPE_BZIP2:
				ret = bzip2_decompress_buf(control, uci);
				break;
			case CTYPE_GZIP:
				ret = gzip_decompress_buf(control, uci);
				break;
			case CTYPE_ZPAQ:
				ret = zpaq_decompress_buf(control, uci, current_thread);
				break;
			case CTYPE_BZIP3:
				ret = bzip3_decompress_buf(control, uci, current_thread);
				break;
			default:
				fatal("Dunno wtf decompression type to use!\n");
				break;
		}
	}
	if (FILTER_USED && uci->streamno == 1) { // restore unfiltered data, literals only
		print_maxverbose("Restoring %s filter data post decompression for thread %'d...\n",
				((control->filter_flag == FILTER_FLAG_X86) ? "x86" :
				((control->filter_flag == FILTER_FLAG_ARM) ? "ARM" :
				((control->filter_flag == FILTER_FLAG_ARMT) ? "ARMT" :
				((control->filter_flag == FILTER_FLAG_PPC) ? "PPC" :
				((control->filter_flag == FILTER_FLAG_SPARC) ? "SPARC" :
				((control->filter_flag == FILTER_FLAG_IA64) ? "IA64" :
				((control->filter_flag == FILTER_FLAG_DELTA) ? "Delta" : "wtf"))))))), current_thread);
		if (control->filter_flag == FILTER_FLAG_X86) {
			UInt32 x86State;
			x86_Convert_Init(x86State);
			x86_Convert(uci->s_buf, uci->u_len, 0, &x86State, 0);
		}
		else if (control->filter_flag == FILTER_FLAG_ARM) {
			ARM_Convert(uci->s_buf, uci->u_len, 0, 0);
		}
		else if (control->filter_flag == FILTER_FLAG_ARMT) {
			ARMT_Convert(uci->s_buf, uci->u_len, 0, 0);
		}
		else if (control->filter_flag == FILTER_FLAG_PPC) {
			PPC_Convert(uci->s_buf, uci->u_len, 0, 0);
		}
		else if (control->filter_flag == FILTER_FLAG_SPARC) {
			SPARC_Convert(uci->s_buf, uci->u_len, 0, 0);
		}
		if (control->filter_flag == FILTER_FLAG_IA64) {
			IA64_Convert(uci->s_buf, uci->u_len, 0, 0);
		}
		if (control->filter_flag == FILTER_FLAG_DELTA) {
			uchar delta_state[DELTA_STATE_SIZE];
			Delta_Init(delta_state);
			Delta_Decode(delta_state, control->delta, uci->s_buf,  uci->u_len);
		}
	}

	/* As per compression, serialise the decompression if it fails in
	 * parallel */
	if (unlikely(ret)) {
		if (unlikely(waited))
			fatal("Failed to decompress in ucompthread\n");
		print_maxverbose("Unable to decompress in parallel, waiting for previous thread to complete before trying again\n");
		/* We do not strictly need to wait for this, so it's used when
		 * decompression fails due to inadequate memory to try again
		 * serialised. */
		lock_mutex(control, &output_lock);
		while (output_thread != current_thread)
			cond_wait(control, &output_cond, &output_lock);
		unlock_mutex(control, &output_lock);
		waited = 1;
		goto retry;
	}

	print_maxverbose("Thread %'d decompressed %'"PRId64" bytes from stream %'d\n", current_thread, uci->u_len, uci->streamno);

	return NULL;
}

/* fill a buffer from a stream - return -1 on failure */
static int fill_buffer(rzip_control *control, struct stream_info *sinfo, struct stream *s, int streamno)
{
	i64 u_len, c_len, last_head, padded_len, header_length, max_len;
	uchar enc_head[25 + SALT_LEN], blocksalt[SALT_LEN];
	struct uncomp_thread *ucthreads = sinfo->ucthreads;
	pthread_t *threads = control->pthreads;
	stream_thread_struct *sts;
	uchar c_type, *s_buf;
	void *thr_return;

	dealloc(s->buf);
	if (s->eos)
		goto out;
fill_another:
	if (unlikely(ucthreads[s->uthread_no].busy))
		fatal("Trying to start a busy thread, this shouldn't happen!\n");

	if (unlikely(read_seekto(control, sinfo, s->last_head)))
		return -1;

	if (ENCRYPT) {
		if (unlikely(read_buf(control, sinfo->fd, enc_head, SALT_LEN)))
			return -1;
		sinfo->total_read += SALT_LEN;
	}

	if (unlikely(read_u8(control, sinfo->fd, &c_type)))
		return -1;

	/* remove checks for lrzip < 0.6 */
	if (control->major_version == 0) {
		int read_len;

		print_maxverbose("Reading ucomp header at %'"PRId64"\n", get_readseek(control, sinfo->fd));
		if (ENCRYPT)
			read_len = 8;
		else
			read_len = sinfo->chunk_bytes;
		if (unlikely(read_val(control, sinfo->fd, &c_len, read_len)))
			return -1;
		if (unlikely(read_val(control, sinfo->fd, &u_len, read_len)))
			return -1;
		if (unlikely(read_val(control, sinfo->fd, &last_head, read_len)))
			return -1;
		header_length = 1 + (read_len * 3);
	}
	sinfo->total_read += header_length;

	if (ENCRYPT) {
		// pass decrypt flag
		if (unlikely(!decrypt_header(control, enc_head, &c_type, &c_len, &u_len, &last_head, LRZ_DECRYPT)))
			return -1;
		if (unlikely(read_buf(control, sinfo->fd, blocksalt, SALT_LEN)))
			return -1;
		sinfo->total_read += SALT_LEN;
	}
	c_len = le64toh(c_len);
	u_len = le64toh(u_len);
	last_head = le64toh(last_head);
	print_maxverbose("Fill_buffer stream %'d c_len %'"PRId64" u_len %'"PRId64" last_head %'"PRId64"\n", streamno, c_len, u_len, last_head);

	/* It is possible for there to be an empty match block at the end of
	 * incompressible data */
	if (unlikely(c_len == 0 && u_len == 0 && streamno == 1 && last_head == 0)) {
		print_maxverbose("Skipping empty match block\n");
		goto skip_empty;
	}

	/* Check for invalid data and that the last_head is actually moving forward correctly.
	 * Check for:
	 * 	compressed length (c_len)
	 * 	uncompressed length (u_len)
	 * 	invalid current stream pointer (last_head < 0)
	 * 	stream pointer extending beyond chunk (last_head > sinfo->size)
	 * 	stream pointer less than last stream pointer (i.e. pointing backwards!)
	 *	Encrypt check requires slightly different test. Doesn't know sinfo->size yet
	 */
	if (ENCRYPT) {
		if (unlikely(c_len < 1 || u_len < 1 || last_head < 0 || (last_head && (last_head <= s->last_head))))
			fatal("Invalid data compressed len %'"PRId64" uncompressed %'"PRId64" last_head %'"PRId64" chunk size %'"PRId64"\n",
			     c_len, u_len, last_head, sinfo->size);
	} else {
		if (unlikely(c_len < 1 || u_len < 1 || last_head < 0 || last_head > sinfo->size ||
				(last_head && (last_head <= s->last_head))))
			fatal("Invalid data compressed len %'"PRId64" uncompressed %'"PRId64" last_head %'"PRId64" chunk size %'"PRId64"\n",
			     c_len, u_len, last_head, sinfo->size);
	}

	/* if encryption used, control->enc_code will be > 0
	 * otherwise length = 0 */
	padded_len = MAX(c_len, *control->enc_keylen);
	sinfo->total_read += padded_len;
	fsync(control->fd_out);

	if (unlikely(u_len > control->maxram))
		print_progress("Warning, attempting to malloc very large buffer for this environment of size %'"PRId64"\n", u_len);
	max_len = MAX(u_len, *control->enc_keylen);
	max_len = MAX(max_len, c_len);
	s_buf = malloc(max_len);
	if (unlikely(!s_buf))
		fatal("Unable to malloc buffer of size %'"PRId64" in fill_buffer\n", u_len);
	sinfo->ram_alloced += u_len;

	if (unlikely(read_buf(control, sinfo->fd, s_buf, padded_len))) {
		dealloc(s_buf);
		return -1;
	}

	// pass decrypt flag
	if (unlikely(ENCRYPT && !lrz_decrypt(control, s_buf, padded_len, blocksalt, LRZ_DECRYPT))) {
		dealloc(s_buf);
		return -1;
	}

	ucthreads[s->uthread_no].s_buf = s_buf;
	ucthreads[s->uthread_no].c_len = c_len;
	ucthreads[s->uthread_no].u_len = u_len;
	ucthreads[s->uthread_no].c_type = c_type;
	ucthreads[s->uthread_no].streamno = streamno;
	s->last_head = last_head;

	/* List this thread as busy */
	ucthreads[s->uthread_no].busy = 1;
	print_maxverbose("Starting thread %d to decompress %'"PRId64" bytes from stream %'d\n",
			 s->uthread_no, padded_len, streamno);

	sts = malloc(sizeof(stream_thread_struct));
	if (unlikely(!sts))
		fatal("Unable to malloc in fill_buffer");
	sts->i = s->uthread_no;
	sts->control = control;
	sts->sinfo = sinfo;
	if (unlikely(!create_pthread(control, &threads[s->uthread_no], NULL, ucompthread, sts))) {
		dealloc(sts);
		return -1;
	}

	if (++s->uthread_no == s->base_thread + s->total_threads)
		s->uthread_no = s->base_thread;
skip_empty:
	/* Reached the end of this stream, no more data to read in, otherwise
	 * see if the next thread is free to grab more data. We also check that
	 * we're not going to be allocating too much ram to generate all these
	 * threads. */
	if (!last_head)
		s->eos = 1;
	else if (s->uthread_no != s->unext_thread && !ucthreads[s->uthread_no].busy &&
		 sinfo->ram_alloced < control->maxram)
			goto fill_another;
out:
	lock_mutex(control, &output_lock);
	output_thread = s->unext_thread;
	cond_broadcast(control, &output_cond);
	unlock_mutex(control, &output_lock);

	/* join_pthread here will make it wait till the data is ready */
	thr_return = NULL;
	if (unlikely(!join_pthread(control, threads[s->unext_thread], &thr_return) || !!thr_return))
		return -1;
	ucthreads[s->unext_thread].busy = 0;

	print_maxverbose("Taking decompressed data from thread %d\n", s->unext_thread);
	s->buf = ucthreads[s->unext_thread].s_buf;
	ucthreads[s->unext_thread].s_buf = NULL;
	s->buflen = ucthreads[s->unext_thread].u_len;
	sinfo->ram_alloced -= s->buflen;
	s->bufp = 0;

	if (++s->unext_thread == s->base_thread + s->total_threads)
		s->unext_thread = s->base_thread;

	return 0;
}

/* write some data to a stream. Return -1 on failure */
void write_stream(rzip_control *control, void *ss, int streamno, uchar *p, i64 len)
{
	struct stream_info *sinfo = ss;

	while (len) {
		i64 n;

		n = MIN(sinfo->bufsize - sinfo->s[streamno].buflen, len);

		memcpy(sinfo->s[streamno].buf + sinfo->s[streamno].buflen, p, n);
		sinfo->s[streamno].buflen += n;
		p += n;
		len -= n;

		/* Flush the buffer every sinfo->bufsize into one thread */
		if (sinfo->s[streamno].buflen == sinfo->bufsize)
			flush_buffer(control, sinfo, streamno);
	}
}

/* read some data from a stream. Return number of bytes read, or -1
   on failure */
i64 read_stream(rzip_control *control, void *ss, int streamno, uchar *p, i64 len)
{
	struct stream_info *sinfo = ss;
	struct stream *s = &sinfo->s[streamno];
	i64 ret = 0;

	while (len) {
		i64 n;

		n = MIN(s->buflen - s->bufp, len);

		if (n > 0) {
			if (unlikely(!s->buf))
				fatal("Stream ran out prematurely, likely corrupt archive\n");
			memcpy(p, s->buf + s->bufp, n);
			s->bufp += n;
			p += n;
			len -= n;
			ret += n;
		}

		if (len && s->bufp == s->buflen) {
			if (unlikely(fill_buffer(control, sinfo, s, streamno)))
				return -1;
			if (s->bufp == s->buflen)
				break;
		}
	}

	return ret;
}

/* flush and close down a stream. return -1 on failure */
int close_stream_out(rzip_control *control, void *ss)
{
	struct stream_info *sinfo = ss;
	int i;

	for (i = 0; i < sinfo->num_streams; i++)
		clear_buffer(control, sinfo, i, 0);

	if (ENCRYPT) {
		/* Last two compressed blocks do not have an offset written
		 * to them so we have to go back and encrypt them now, but we
		 * must wait till the threads return. */
		int close_thread = output_thread;

		for (i = 0; i < control->threads; i++) {
			cksem_wait(control, &cthreads[close_thread].cksem);
			cksem_post(control, &cthreads[close_thread].cksem);
			if (++close_thread == control->threads)
				close_thread = 0;
		}
		for (i = 0; i < sinfo->num_streams; i++)
			rewrite_encrypted(control, sinfo, sinfo->s[i].last_headofs);
	}

	/* Note that sinfo->s and sinfo are not released here but after compression
	* has completed as they cannot be freed immediately because their values
	* are read after the next stream has started.
	*/
	return 0;
}

/* Add to an runzip list to safely deallocate memory after all threads have
 * returned. */
static void add_to_rulist(rzip_control *control, struct stream_info *sinfo)
{
	struct runzip_node *node = calloc(sizeof(struct runzip_node), 1);

	if (unlikely(!node))
		fatal("Failed to calloc struct node in add_rulist\n");
	node->sinfo = sinfo;
	node->pthreads = control->pthreads;
	node->prev = control->rulist;
	control->ruhead = node;
}

/* close down an input stream */
int close_stream_in(rzip_control *control, void *ss)
{
	struct stream_info *sinfo = ss;
	int i;

	print_maxverbose("Closing stream at %'"PRId64", want to seek to %'"PRId64"\n",
			 get_readseek(control, control->fd_in),
			 sinfo->initial_pos + sinfo->total_read);
	if (unlikely(read_seekto(control, sinfo, sinfo->total_read)))
		return -1;

	for (i = 0; i < sinfo->num_streams; i++)
		dealloc(sinfo->s[i].buf);

	output_thread = 0;
	/* We cannot safely release the sinfo and pthread data here till all
	 * threads are shut down. */
	add_to_rulist(control, sinfo);

	return 0;
}

/* As others are slow and lz4 very fast, it is worth doing a quick lz4 pass
   to see if there is any compression at all with lz4 first. It is unlikely
   that others will be able to compress if lz4 is unable to drop a single byte
   so do not compress any block that is incompressible by lz4. */
static int lz4_compresses(rzip_control *control, uchar *s_buf, i64 s_len)
{
	int in_len, d_len, buftest_size;
	i64 test_len = s_len;
	char *c_buf = NULL, *test_buf = (char *)s_buf;
	/* set minimum buffer test size based on the length of the test stream */
	double pct = 101;	/* set to failure */
	int return_value;
	int workcounter = 0;	/* count # of passes */
	int lz4_ret;

	in_len = MIN(test_len, STREAM_BUFSIZE); // Set min of s_len or 10MB
	buftest_size = in_len;
	d_len = in_len+1;

	c_buf = malloc(d_len);
	if (unlikely(!c_buf))
		fatal("Unable to allocate c_buf in lz4_compresses\n");

	/* Test progressively larger blocks at a time and as soon as anything
	   compressible is found, including with Threshold, jump out as a success */
	while (test_len > 0) {
		workcounter++;
		lz4_ret = LZ4_compress_default((const char *)test_buf, c_buf, in_len, d_len);

		/* Apply threshold limit if applicable */
		if (lz4_ret > 0) {
			pct = 100 * ((double) lz4_ret / (double) in_len);
			if (lz4_ret < in_len * ((double) control->threshold / 100))
				break;
		}

		/* expand and move buffer */
		test_len -= in_len;
		if (test_len > 0) {
			// No. Don't move ptr. Just increase length tested
			buftest_size += in_len;
			if (buftest_size < STREAM_BUFSIZE)
				buftest_size <<= 1;
			in_len = MIN(test_len, buftest_size);
			d_len = in_len+1;		// Make sure dlen is increased as buffer test does
			c_buf = realloc(c_buf, d_len);
			if (unlikely(!c_buf))
				fatal("Unable to reallocate c_buf in lz4_compresses\n");
		}
	}
	/* if pct >0 and <1 round up so return value won't show failed */
	return_value = (int) (pct > control->threshold ? 0 : pct < 1 ? pct+1 : pct);
	print_maxverbose("lz4 testing %s for chunk %'"PRId64". Compressed size = %5.2F%% of test size %'d, %'d Passes\n",
			(return_value > 0 ? "OK" : "FAILED"), s_len,
			pct, buftest_size, workcounter);

	dealloc(c_buf);

	return return_value;
}
