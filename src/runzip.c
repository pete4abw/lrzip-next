/*
   Copyright (C) 2006-2016,2018, 2021 Con Kolivas
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
/* rzip decompression algorithm */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/types.h>
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_ENDIAN_H
# include <endian.h>
#elif HAVE_SYS_ENDIAN_H
# include <sys/endian.h>
#endif
#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif

#include "runzip.h"
#include "stream.h"
#include "util.h"
#include "lrzip_core.h"

/* Work Function to compute hash of a file stream */
int hash_stream ( FILE *, uchar *, int, int );

static inline uchar read_u8(rzip_control *control, void *ss, int stream, bool *err)
{
	uchar b;

	if (unlikely(read_stream(control, ss, stream, &b, 1) != 1)) {
		*err = true;
		fatal_return(("Stream read u8 failed\n"), 0);
	}
	return b;
}

static inline u32 read_u32(rzip_control *control, void *ss, int stream, bool *err)
{
	u32 ret;

	if (unlikely(read_stream(control, ss, stream, (uchar *)&ret, 4) != 4)) {
		*err = true;
		fatal_return(("Stream read u32 failed\n"), 0);
	}
	ret = le32toh(ret);
	return ret;
}

/* Read a variable length of chars dependant on how big the chunk was */
static inline i64 read_vchars(rzip_control *control, void *ss, int stream, int length)
{
	i64 s = 0;

	if (unlikely(read_stream(control, ss, stream, (uchar *)&s, length) != length))
		fatal_return(("Stream read of %'d bytes failed\n", length), -1);
	s = le64toh(s);
	return s;
}

static i64 seekcur_fdout(rzip_control *control)
{
	if (!TMP_OUTBUF)
		return lseek(control->fd_out, 0, SEEK_CUR);
	return (control->out_relofs + control->out_ofs);
}

static i64 seekto_fdhist(rzip_control *control, i64 pos)
{
	if (!TMP_OUTBUF)
		return lseek(control->fd_hist, pos, SEEK_SET);
	control->hist_ofs = pos - control->out_relofs;
	if (control->hist_ofs > control->out_len)
		control->out_len = control->hist_ofs;
	if (unlikely(control->hist_ofs < 0 || control->hist_ofs > control->out_maxlen)) {
		print_err("Trying to seek outside tmpoutbuf to %'"PRId64" in seekto_fdhist\n", control->hist_ofs);
		return -1;
	}
	return pos;
}

static i64 seekcur_fdin(rzip_control *control)
{
	if (!TMP_INBUF)
		return lseek(control->fd_in, 0, SEEK_CUR);
	return control->in_ofs;
}

static i64 seekto_fdin(rzip_control *control, i64 pos)
{
	if (!TMP_INBUF)
		return lseek(control->fd_in, pos, SEEK_SET);
	if (unlikely(pos > control->in_len || pos < 0)) {
		print_err("Trying to seek outside tmpinbuf to %'"PRId64" in seekto_fdin\n", pos);
		return -1;
	}
	control->in_ofs = pos;
	return 0;
}

static i64 seekto_fdinend(rzip_control *control)
{
	int tmpchar;

	if (!TMP_INBUF)
		return lseek(control->fd_in, 0, SEEK_END);
	while ((tmpchar = getchar()) != EOF) {
		control->tmp_inbuf[control->in_len++] = (char)tmpchar;
		if (unlikely(control->in_len > control->in_maxlen))
			failure_return(("Trying to read greater than max_len\n"), -1);
	}
	control->in_ofs = control->in_len;
	return control->in_ofs;
}

static i64 read_header(rzip_control *control, void *ss, uchar *head)
{
	bool err = false;

	*head = read_u8(control, ss, 0, &err);
	if (err)
		return -1;
	return read_vchars(control, ss, 0, control->chunk_bytes);
}

static i64 unzip_literal(rzip_control *control, void *ss, i64 len)
{
	i64 stream_read;
	uchar *buf;

	if (unlikely(len < 0))
		failure_return(("len %'"PRId64" is negative in unzip_literal!\n",len), -1);

	buf = (uchar *)malloc(len);
	if (unlikely(!buf))
		fatal_return(("Failed to malloc literal buffer of size %'"PRId64"\n", len), -1);

	stream_read = read_stream(control, ss, 1, buf, len);
	if (unlikely(stream_read == -1 )) {
		dealloc(buf);
		fatal_return(("Failed to read_stream in unzip_literal\n"), -1);
	}

	if (unlikely(write_1g(control, buf, (size_t)stream_read) != (ssize_t)stream_read)) {
		dealloc(buf);
		fatal_return(("Failed to write literal buffer of size %'"PRId64"\n", stream_read), -1);
	}

	if (!HAS_HASH)
//		*cksum = CrcUpdate(*cksum, buf, stream_read);
		gcry_md_write(control->crc_handle, buf, stream_read);
	if (HAS_HASH)
		gcry_md_write(control->hash_handle, buf, stream_read);

	dealloc(buf);
	return stream_read;
}

static i64 read_fdhist(rzip_control *control, void *buf, i64 len)
{
	if (!TMP_OUTBUF)
		return read_1g(control, control->fd_hist, buf, len);
	if (unlikely(len + control->hist_ofs > control->out_maxlen)) {
		print_err("Trying to read beyond end of tmpoutbuf in read_fdhist\n");
		return -1;
	}
	memcpy(buf, control->tmp_outbuf + control->hist_ofs, len);
	return len;
}

static i64 unzip_match(rzip_control *control, void *ss, i64 len, int chunk_bytes)
{
	i64 offset, n, total, cur_pos;
	uchar *buf;

	if (unlikely(len < 0))
		failure_return(("len %'"PRId64" is negative in unzip_match!\n",len), -1);

	total = 0;
	cur_pos = seekcur_fdout(control);
	if (unlikely(cur_pos == -1))
		fatal_return(("Seek failed on out file in unzip_match.\n"), -1);

	/* Note the offset is in a different format v0.40+ */
	offset = read_vchars(control, ss, 0, chunk_bytes);
	if (unlikely(offset == -1))
		return -1;
	if (unlikely(seekto_fdhist(control, cur_pos - offset) == -1))
		fatal_return(("Seek failed by %'d from %'d on history file in unzip_match\n",
		      offset, cur_pos), -1);

	n = MIN(len, offset);
	if (unlikely(n < 1))
		fatal_return(("Failed fd history in unzip_match due to corrupt archive\n"), -1);

	buf = (uchar *)malloc(n);
	if (unlikely(!buf))
		fatal_return(("Failed to malloc match buffer of size %'"PRId64"\n", len), -1);

	if (unlikely(read_fdhist(control, buf, (size_t)n) != (ssize_t)n)) {
		dealloc(buf);
		fatal_return(("Failed to read %d bytes in unzip_match\n", n), -1);
	}

	while (len) {
		n = MIN(len, offset);
		if (unlikely(n < 1))
			fatal_return(("Failed fd history in unzip_match due to corrupt archive\n"), -1);

		if (unlikely(write_1g(control, buf, (size_t)n) != (ssize_t)n)) {
			dealloc(buf);
			fatal_return(("Failed to read %'d bytes in unzip_match\n", n), -1);
		}

		if (!HAS_HASH)
//			*cksum = CrcUpdate(*cksum, buf, n);
			gcry_md_write(control->crc_handle, buf, n);
		if (HAS_HASH)
			gcry_md_write(control->hash_handle, buf, n);

		len -= n;
		total += n;
	}

	dealloc(buf);

	return total;
}

void clear_rulist(rzip_control *control)
{
	while (control->ruhead) {
		struct runzip_node *node = control->ruhead;
		struct stream_info *sinfo = node->sinfo;

		dealloc(sinfo->ucthreads);
		dealloc(node->pthreads);
		dealloc(sinfo->s);
		dealloc(sinfo);
		control->ruhead = node->prev;
		dealloc(node);
	}
}

/* decompress a section of an open file. Call fatal_return(() on error
   return the number of bytes that have been retrieved
 */
static i64 runzip_chunk(rzip_control *control, int fd_in, i64 expected_size, i64 tally)
{
	uint32 good_cksum, cksum = 0;
	i64 len, ofs, total = 0;
	int p;
	char chunk_bytes;
	struct stat st;
	uchar head;
	void *ss;
	bool err = false;
	struct timeval curtime, lasttime;
	lasttime.tv_sec = 0;

	/* for display of progress */
	unsigned long divisor[] = {1,1024,1048576,1073741824U};
	char *suffix[] = {"","KB","MB","GB"};
	double prog_done, prog_tsize;
	int divisor_index;

	if (expected_size > (i64)10737418240ULL)	/* > 10GB */
		divisor_index = 3;
	else if (expected_size > 10485760)	/* > 10MB */
		divisor_index = 2;
	else if (expected_size > 10240)	/* > 10KB */
		divisor_index = 1;
	else
		divisor_index = 0;

	prog_tsize = (long double)expected_size / (long double)divisor[divisor_index];

	/* Determine the chunk_byte width size. Versions < 0.4 used 4
	 * bytes for all offsets, version 0.4 used 8 bytes. Versions 0.5+ use
	 * a variable number of bytes depending on chunk size.*/
	if (control->major_version == 0 && control->minor_version < 4)
		chunk_bytes = 4;
	else if (control->major_version == 0 && control->minor_version == 4)
		chunk_bytes = 8;
	else {
		print_maxverbose("Reading chunk_bytes at %'"PRId64"\n", get_readseek(control, fd_in));
		/* Read in the stored chunk byte width from the file */
		if (unlikely(read_1g(control, fd_in, &chunk_bytes, 1) != 1))
			fatal_return(("Failed to read chunk_bytes size in runzip_chunk\n"), -1);
		if (unlikely(chunk_bytes < 1 || chunk_bytes > 8))
			failure_return(("chunk_bytes %'d is invalid in runzip_chunk\n", chunk_bytes), -1);
	}
	if (!tally && expected_size)
		print_maxverbose("Expected size: %'"PRId64"\n", expected_size);
	print_maxverbose("Chunk byte width: %'d\n", chunk_bytes);

	ofs = seekcur_fdin(control);
	if (unlikely(ofs == -1))
		fatal_return(("Failed to seek input file in runzip_fd\n"), -1);

	if (fstat(fd_in, &st) || st.st_size - ofs == 0)
		return 0;

	ss = open_stream_in(control, fd_in, NUM_STREAMS, chunk_bytes);
	if (unlikely(!ss))
		failure_return(("Failed to open_stream_in in runzip_chunk\n"), -1);

	/* All chunks were unnecessarily encoded 8 bytes wide version 0.4x */
	if (control->major_version == 0 && control->minor_version == 4)
		control->chunk_bytes = 8;
	else
		control->chunk_bytes = 2;

	while ((len = read_header(control, ss, &head)) || head) {
		i64 u;
		if (unlikely(len == -1))
			return -1;
		switch (head) {
			case 0:
				u = unzip_literal(control, ss, len);
				if (unlikely(u == -1)) {
					close_stream_in(control, ss);
					return -1;
				}
				total += u;
				break;

			default:
				u = unzip_match(control, ss, len, chunk_bytes);
				if (unlikely(u == -1)) {
					close_stream_in(control, ss);
					return -1;
				}
				total += u;
				break;
		}
		if (expected_size) {
			p = 100 * ((double)(tally + total) / (double)expected_size);
			gettimeofday(&curtime,NULL);
			if (curtime.tv_sec - lasttime.tv_sec > 5 || p == 100 ) {	/* update every 5 seconds or when done */
				prog_done = (double)(tally + total) / (double)divisor[divisor_index];
				print_progress("%3d%%  %9.2f / %9.2f %s\r",
						p, prog_done, prog_tsize, suffix[divisor_index] );
				lasttime.tv_sec = curtime.tv_sec;
			}
		}
	}

	memcpy(&cksum, gcry_md_read(control->crc_handle, *control->crc_gcode), *control->crc_len);
	if (!HAS_HASH) {
		good_cksum = read_u32(control, ss, 0, &err);
		if (unlikely(err)) {
			close_stream_in(control, ss);
			return -1;
		}
		if (unlikely(good_cksum != cksum)) {
			close_stream_in(control, ss);
			failure_return(("Bad checksum: 0x%08x - expected: 0x%08x\n", cksum, good_cksum), -1);
		}
		print_maxverbose("Checksum for block: 0x%08x\n", cksum);
	}

	if (unlikely(close_stream_in(control, ss)))
		fatal("Failed to close stream!\n");

	return total;
}

/* Decompress an open file. Call fatal_return(() on error
   return the number of bytes that have been retrieved
 */
i64 runzip_fd(rzip_control *control, int fd_in, int fd_out, int fd_hist, i64 expected_size)
{
	uchar *hash_stored;
	struct timeval start,end;
	i64 total = 0, u;
	double tdiff;

	hash_stored = calloc(*control->hash_len, 1);

	gcry_md_open(&control->crc_handle, *control->crc_gcode, GCRY_MD_FLAG_SECURE);
	if (HAS_HASH) {
		gcry_md_open(&control->hash_handle, *control->hash_gcode, GCRY_MD_FLAG_SECURE);
		if ((unlikely(control->hash_handle == NULL)))
			failure("Unable to set %s handle in runzip_fd\n", control->hash_label);
	}
	gettimeofday(&start,NULL);

	do {
		u = runzip_chunk(control, fd_in, expected_size, total);
		if (u < 1) {
			if (u < 0 || total < expected_size) {
				print_err("Failed to runzip_chunk in runzip_fd\n");
				return -1;
			}
		}
		total += u;
		if (TMP_OUTBUF) {
			if (unlikely(!flush_tmpoutbuf(control))) {
				print_err("Failed to flush_tmpoutbuf in runzip_fd\n");
				return -1;
			}
		} else if (STDOUT) {
			if (unlikely(!dump_tmpoutfile(control, fd_out))) {
				print_err("Failed to dump_tmpoutfile in runzip_fd\n");
				return -1;
			}
		}
		if (TMP_INBUF)
			clear_tmpinbuf(control);
		else if (STDIN && !DECOMPRESS) {
			if (unlikely(!clear_tmpinfile(control))) {
				print_err("Failed to clear_tmpinfile in runzip_fd\n");
				return -1;
			}
		}
	} while (total < expected_size || (!expected_size && !control->eof));

	gettimeofday(&end,NULL);
	if (!ENCRYPT) {
		tdiff = end.tv_sec - start.tv_sec;
		if (!tdiff)
			tdiff = 1;
		print_progress("\nAverage DeCompression Speed: %6.3fMB/s\n",
			       (total / ONE_MB) / tdiff);
	}

	if (HAS_HASH) {
		int i,j;

		/* if we're using an XOF function, i.e. SLACK128, then use md_extract */
		if ( control->hash_code < SHAKE128_16 )
			memcpy(control->hash_resblock, gcry_md_read(control->hash_handle, *control->hash_gcode),
					*control->hash_len);
		else
			gcry_md_extract(control->hash_handle, *control->hash_gcode, control->hash_resblock, *control->hash_len);

		i64 fdinend = seekto_fdinend(control);
		if (unlikely(fdinend == -1))
			failure_return(("Failed to seekto_fdinend in rzip_fd\n"), -1);
		if (unlikely(seekto_fdin(control, fdinend - *control->hash_len) == -1))
			failure_return(("Failed to seekto_fdin in rzip_fd\n"), -1);

		if (unlikely(read_1g(control, fd_in, hash_stored, *control->hash_len) != *control->hash_len))
			fatal_return(("Failed to read %s data in runzip_fd\n", control->hash_label), -1);
		if (ENCRYPT)
			// pass decrypt flag
			if (unlikely(!lrz_decrypt(control, hash_stored, *control->hash_len, control->salt_pass, LRZ_DECRYPT)))
				return -1;
		for (i = 0; i < *control->hash_len; i++) {
			if (hash_stored[i] != control->hash_resblock[i]) {
				print_output("%s CHECK FAILED.\nStored:", control->hash_label);
				for (j = 0; j < *control->hash_len; j++)
					print_output("%02x", hash_stored[j]);
				print_output("\nOutput file:");
				for (j = 0; j < *control->hash_len; j++)
					print_output("%02x", control->hash_resblock[j]);
				failure_return(("\n"), -1);
			}
		}

		print_output("%s:", control->hash_label);
		for (i = 0; i < *control->hash_len; i++)
			print_output("%02x", control->hash_resblock[i]);
		print_output("\n");

		if (CHECK_FILE) {
			FILE *hash_fstream;
			int i, j;

			if (TMP_OUTBUF)
				close_tmpoutbuf(control);
			memcpy(hash_stored, control->hash_resblock, *control->hash_len);
			if (unlikely(seekto_fdhist(control, 0) == -1))
				fatal_return(("Failed to seekto_fdhist in runzip_fd\n"), -1);
			if (unlikely((hash_fstream = fdopen(fd_hist, "r")) == NULL))
				fatal_return(("Failed to fdopen fd_hist in runzip_fd\n"), -1);
			if (unlikely(hash_stream(hash_fstream, control->hash_resblock, *control->hash_gcode, *control->hash_len)))
				fatal_return(("Failed to %s_stream in runzip_fd\n", control->hash_label), -1);
			/* We don't close the file here as it's closed in main */
			for (i = 0; i < *control->hash_len; i++) {
				if (hash_stored[i] != control->hash_resblock[i]) {
					print_output("%s CHECK FAILED.\nStored: ", control->hash_label);
					for (j = 0; j < *control->hash_len; j++)
						print_output("%02x", hash_stored[j]);
					print_output("\nOutput file:");
					for (j = 0; j < *control->hash_len; j++)
						print_output("%02x", control->hash_resblock[j]);
					failure_return(("\n"), -1);
				}
			}
			print_output("%s integrity of written file matches archive\n", control->hash_label);
		}
	} else	/* hash not stored */
		print_output("Note this lrzip archive did not have a stored hash value.\n"
				"The archive decompression was validated with crc32 and the was "
				"calculated on decompression\n");

	free(hash_stored);

	return total;
}

/* Work Function to compute a hash from a file stream
 * Taken from the old md5.c file and updated to use gcrypt
 */

/* Compute hash message digest for bytes read from STREAM.  The
   resulting message digest number will be written into the 16 bytes
   beginning at RESBLOCK.  */
#define BLOCKSIZE 32768
int hash_stream (FILE *stream, uchar *resblock, int hash_gcode, int hash_len)
{
	gcry_md_hd_t hash_handle;
	size_t sum;

	char *buffer = malloc (BLOCKSIZE + 72);
	if (!buffer)
		return 1;

	gcry_md_open(&hash_handle, hash_gcode, GCRY_MD_FLAG_SECURE);

	/* Iterate over full file contents.  */
	while (1)
	{
		size_t n;
		sum = 0;

		/* Read block.  Take care for partial reads.  */
		while (1)
		{
			n = fread (buffer + sum, 1, BLOCKSIZE - sum, stream);
			sum += n;
			if (sum == BLOCKSIZE)
				break;

			if (n == 0)
			{
				if (ferror (stream))
				{
					free (buffer);
					return 1;
				}
				goto process_partial_block;
			}
			if (feof (stream))
				goto process_partial_block;
		}
		gcry_md_write(hash_handle, buffer, BLOCKSIZE);
	}

process_partial_block:
	/* Process any remaining bytes.  */
	if (sum > 0)
		gcry_md_write(hash_handle, buffer, sum);

	/* Construct result in desired memory.  */
	/* test for XOF */
	if ( hash_gcode < SHAKE128_16)
		memcpy(resblock, gcry_md_read(hash_handle, hash_gcode), hash_len);
	else
		gcry_md_extract(hash_handle, hash_gcode, resblock, hash_len);

	gcry_md_close(hash_handle);
	free (buffer);
	return 0;
}
