/*
   Copyright (C) 2006-2016 Con Kolivas
   Copyright (C) 2011 Peter Hyman
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
#ifndef LRZIP_UTIL_H
#define LRZIP_UTIL_H

#include "lrzip_private.h"
#include <errno.h>
#include <semaphore.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>

void register_infile(rzip_control *control, const char *name, char delete);
void register_outfile(rzip_control *control, const char *name, char delete);
void unlink_files(rzip_control *control);
void register_outputfile(rzip_control *control, FILE *f);
void fatal_exit(rzip_control *control);
void setup_overhead(rzip_control *control);
void setup_ram(rzip_control *control);
void round_to_page(i64 *size);
size_t round_up_page(rzip_control *control, size_t len);
bool read_config(rzip_control *control);
void lrz_stretch(rzip_control *control);
bool lrz_crypt(const rzip_control *control, uchar *buf, i64 len, const uchar *salt, int encrypt);
/* decrypt_header will take a final variable for either decrypt or validate.
 * Valdidate will suppress printing message during validation or info
 */
bool decrypt_header(rzip_control *control, uchar *head, uchar *c_type, i64 *c_len, i64 *u_len, i64 *last_head, int decompress_type);

static inline void fatal(const rzip_control *control, unsigned int line, const char *file, const char *func, const char *format, ...)
{
	va_list ap;
	/* lrzip library callback code removed */
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	fatal_exit((rzip_control*)control);
}
#ifdef fatal
# undef fatal
#endif
#define fatal(...) fatal(control, __LINE__, __FILE__, __func__, __VA_ARGS__)

static inline bool lrz_encrypt(const rzip_control *control, uchar *buf, i64 len, const uchar *salt)
{
	return lrz_crypt(control, buf, len, salt, LRZ_ENCRYPT);
}

static inline bool lrz_decrypt(const rzip_control *control, uchar *buf, i64 len, const uchar *salt, int dec_or_validate)
{
	return lrz_crypt(control, buf, len, salt, dec_or_validate);
}

/* ck specific wrappers for true unnamed semaphore usage on platforms
 * that support them and for apple which does not. We use a single byte across
 * a pipe to emulate semaphore behaviour there. */
#ifdef __APPLE__
static inline void cksem_init(const rzip_control *control, cksem_t *cksem)
{
	int flags, fd, i;

	if (pipe(cksem->pipefd) == -1)
		fatal("Failed pipe errno=%d", errno);

	/* Make the pipes FD_CLOEXEC to allow them to close should we call
	 * execv on restart. */
	for (i = 0; i < 2; i++) {
		fd = cksem->pipefd[i];
		flags = fcntl(fd, F_GETFD, 0);
		flags |= FD_CLOEXEC;
		if (fcntl(fd, F_SETFD, flags) == -1)
			fatal("Failed to fcntl errno=%d", errno);
	}
}

static inline void cksem_post(const rzip_control *control, cksem_t *cksem)
{
	const char buf = 1;
	int ret;

	ret = write(cksem->pipefd[1], &buf, 1);
	if (unlikely(ret == 0))
		fatal("Failed to write in cksem_post errno=%d", errno);
}

static inline void cksem_wait(const rzip_control *control, cksem_t *cksem)
{
	char buf;
	int ret;

	ret = read(cksem->pipefd[0], &buf, 1);
	if (unlikely(ret == 0))
		fatal("Failed to read in cksem_post errno=%d", errno);
}
#else
static inline void cksem_init(const rzip_control *control, cksem_t *cksem)
{
	int ret;
	if ((ret = sem_init(cksem, 0, 0)))
		fatal("Failed to sem_init ret=%d errno=%d", ret, errno);
}

static inline void cksem_post(const rzip_control *control, cksem_t *cksem)
{
	if (unlikely(sem_post(cksem)))
		fatal("Failed to sem_post errno=%d cksem=0x%p", errno, cksem);
}

static inline void cksem_wait(const rzip_control *control, cksem_t *cksem)
{
	if (unlikely(sem_wait(cksem)))
		fatal("Failed to sem_wait errno=%d cksem=0x%p", errno, cksem);
}
#endif

#endif
