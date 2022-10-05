/*
   Copyright (C) 2006-2016, 2021 Con Kolivas
   Copyright (C) 2011 Serge Belyshev
   Copyright (C) 2008, 2011, 2019, 2020, 2021 Peter Hyman
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

/*
  Utilities used in rzip

  tridge, June 1996
  */

/*
 * Realloc removed
 * Functions added
 *    read_config()
 * Peter Hyman, December 2008
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdarg.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <termios.h>

#ifdef _SC_PAGE_SIZE
# define PAGE_SIZE (sysconf(_SC_PAGE_SIZE))
#else
# define PAGE_SIZE (4096)
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "lrzip_private.h"
#include "util.h"
#ifdef HAVE_CTYPE_H
# include <ctype.h>
#endif

/* Macros for testing parameters */
#define isparameter( parmstring, value )	(!strcasecmp( parmstring, value ))
#define iscaseparameter( parmvalue, value )	(!strcmp( parmvalue, value ))

void register_infile(rzip_control *control, const char *name, char delete)
{
	control->util_infile = name;
	control->delete_infile = delete;
}

void register_outfile(rzip_control *control, const char *name, char delete)
{
	control->util_outfile = name;
	control->delete_outfile = delete;
}

void register_outputfile(rzip_control *control, FILE *f)
{
	control->outputfile = f;
}

void unlink_files(rzip_control *control)
{
	/* Delete temporary files generated for testing or faking stdio */
	if (control->util_outfile && control->delete_outfile)
		unlink(control->util_outfile);

	if (control->util_infile && control->delete_infile)
		unlink(control->util_infile);
}

void fatal_exit(rzip_control *control)
{
	struct termios termios_p;

	/* Make sure we haven't died after disabling stdin echo */
	tcgetattr(fileno(stdin), &termios_p);
	termios_p.c_lflag |= ECHO;
	tcsetattr(fileno(stdin), 0, &termios_p);

	unlink_files(control);
	if (!STDOUT && !TEST_ONLY && control->outfile) {
		if (!KEEP_BROKEN) {
			print_verbose("Deleting broken file %s\n", control->outfile);
			unlink(control->outfile);
		} else
			print_verbose("Keeping broken file %s as requested\n", control->outfile);
	}
	fprintf(control->outputfile, "Fatal error - exiting\n");
	fflush(control->outputfile);
	exit(1);
}

void setup_overhead(rzip_control *control)
{
	/* Work out the compression overhead per compression thread for the
	 * compression back-ends that need a lot of ram
	 * and set Dictionary size */
	if (LZMA_COMPRESS) {
		if (control->dictSize == 0) {
			switch (control->compression_level) {
			case 1:
			case 2:
			case 3: control->dictSize = (1 << (control->compression_level * 2 + 16));
				break; // 256KB to 4MB
			case 4:
			case 5:
			case 6: control->dictSize = (1 << (control->compression_level + 19));
				break; // 8MB to 32MB
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
		/* LZMA spec shows memory requirements as 6MB, not 4MB and state size
		 * where default is 16KB */
		control->overhead = ((i64)control->dictSize * 23 / 2) + (6 * ONE_MB) + 16384;
	} else if (ZPAQ_COMPRESS) {
		control->zpaq_level = (control->compression_level < 4 ? 3 :
					(control->compression_level < 8 ? 4 : 5));
		if (control->zpaq_bs == 0) {
			switch (control->compression_level) {
			case 1:
			case 2:
			case 3: control->zpaq_bs = 4;
				break;  //16MB ZPAQ Default
			case 4:
			case 5:	control->zpaq_bs = 5;
				break;	//32MB
			case 6:	control->zpaq_bs = 6;
				break;	//64MB
			case 7:	control->zpaq_bs = 7;
				break;	//128MB
			case 8:	control->zpaq_bs = 8;
				break;	//256MB
			case 9:	control->zpaq_bs = 9;
				break;	//512MB
			default: control->zpaq_bs = 4;
				break;	// should never reach here
			}
		}
		control->overhead = (i64) (1 << control->zpaq_bs) * ONE_MB;	// times 8 or 16. Out for now
	} else if(BZIP3_COMPRESS) {
		if(control->bzip3_bs == 0) {
			switch (control->compression_level) {
			case 1:
			case 2:
			case 3:
			case 4:
			case 5:	control->bzip3_bs = 5;
				break;	//32MB
			case 6:	control->bzip3_bs = 6;
				break;	//64MB
			case 7:	control->bzip3_bs = 7;
				break;	//128MB
			case 8: case 9:	control->bzip3_bs = 8;
				break;	//256MB
			default: control->bzip3_bs = 5;
				break;	// should never reach here
			}
		}

		control->overhead = (i64) (1 << control->bzip3_bs) * ONE_MB * 6;
	}

	/* no need for zpaq computation here. do in open_stream_out() */
}

void setup_ram(rzip_control *control)
{
	/* Use less ram when using STDOUT to store the temporary output file. */
	if (STDOUT && ((STDIN && DECOMPRESS) || !(DECOMPRESS || TEST_ONLY)))
		control->maxram = control->ramsize / 6;
	else
		control->maxram = control->ramsize / 3;
	control->usable_ram = control->maxram;
	round_to_page(&control->maxram);
}

void round_to_page(i64 *size)
{
	*size -= *size % PAGE_SIZE;
	if (unlikely(!*size))
		*size = PAGE_SIZE;
}

size_t round_up_page(rzip_control *control, size_t len)
{
	int rem = len % control->page_size;

	if (rem)
		len += control->page_size - rem;
	return len;
}

bool read_config(rzip_control *control)
{
	/* check for lrzip.conf in ., $HOME/.lrzip and /etc/lrzip */
	char *HOME, homeconf[255];
	char *parametervalue;
	char *parameter;
	char line[255];
	FILE *fp;

	fp = fopen("lrzip.conf", "r");
	if (fp)
		fprintf(control->msgout, "Using configuration file ./lrzip.conf\n");
	if (fp == NULL) {
		HOME=getenv("HOME");
		if (HOME) {
			snprintf(homeconf, sizeof(homeconf), "%s/.lrzip/lrzip.conf", HOME);
			fp = fopen(homeconf, "r");
			if (fp)
				fprintf(control->msgout, "Using configuration file %s\n", homeconf);
		}
	}
	if (fp == NULL) {
		fp = fopen("/etc/lrzip/lrzip.conf", "r");
		if (fp)
			fprintf(control->msgout, "Using configuration file /etc/lrzip/lrzip.conf\n");
	}
	if (fp == NULL)
		return false;

	/* if we get here, we have a file. read until no more. */

	while ((fgets(line, 255, fp)) != NULL) {
		if (strlen(line))
			line[strlen(line) - 1] = '\0';
		parameter = strtok(line, " =");
		if (parameter == NULL)
			continue;
		/* skip if whitespace or # */
		if (isspace(*parameter))
			continue;
		if (*parameter == '#')
			continue;

		parametervalue = strtok(NULL, " =");
		if (parametervalue == NULL)
			continue;

		/* have valid parameter line, now assign to control */

		if (isparameter(parameter, "window")) {
			control->window = atoi(parametervalue);
		}
		else if (isparameter(parameter, "unlimited")) {
			if (isparameter(parametervalue, "yes"))
				control->flags |= FLAG_UNLIMITED;
		}
		else if (isparameter(parameter, "compressionlevel")) {
			control->compression_level = atoi(parametervalue);
			if ( control->compression_level < 1 || control->compression_level > 9 )
				fatal("CONF.FILE error. Compression Level must between 1 and 9\n");
		}
		else if (isparameter(parameter, "rziplevel")) {
			control->rzip_compression_level = atoi(parametervalue);
			if ( control->rzip_compression_level < 1 || control->rzip_compression_level > 9 )
				fatal("CONF.FILE error. RZIP Compression Level must between 1 and 9\n");
		}
		else if (isparameter(parameter, "compressionmethod")) {
			/* valid are rzip, gzip, bzip2, lzo, lzma (default), and zpaq */
			if (control->flags & FLAG_NOT_LZMA)
				fatal("CONF.FILE error. Can only specify one compression method\n");
			if (isparameter(parametervalue, "bzip2"))
				control->flags |= FLAG_BZIP2_COMPRESS;
			else if (isparameter(parametervalue, "gzip"))
				control->flags |= FLAG_ZLIB_COMPRESS;
			else if (isparameter(parametervalue, "lzo"))
				control->flags |= FLAG_LZO_COMPRESS;
			else if (isparameter(parametervalue, "rzip"))
				control->flags |= FLAG_NO_COMPRESS;
			else if (isparameter(parametervalue, "zpaq"))
				control->flags |= FLAG_ZPAQ_COMPRESS;
			else if (isparameter(parametervalue, "bzip3"))
				control->flags |= FLAG_BZIP3_COMPRESS;
			else if (!isparameter(parametervalue, "lzma")) /* oops, not lzma! */
				fatal("CONF.FILE error. Invalid compression method %s specified\n", parametervalue);
		}
		else if (isparameter(parameter, "lzotest")) {
			/* default is yes */
			if (isparameter(parametervalue, "no"))
				control->flags &= ~FLAG_THRESHOLD;
		}
		else if (isparameter(parameter, "threshold")) {
			/* default is 100 */
			control->threshold = atoi(parametervalue);
			if (control->threshold < 1 || control->threshold > 99)
				fatal("CONF.FILE error. LZO Threshold must be between 1 and 99\n");
		}
		else if (isparameter(parameter, "hashcheck")) {
			if (isparameter(parametervalue, "yes")) {
				control->flags |= FLAG_CHECK;
				control->flags |= FLAG_HASH;
			}
		}
		else if (isparameter(parameter, "hash")) {
			control->hash_code = atoi(parametervalue);
			if (isparameter(parametervalue, "yes") ||
				(control->hash_code > 0 && control->hash_code <= MAXHASH))
				control->flags |= FLAG_HASHED;
			else if (control->hash_code > MAXHASH) {	// out of bounds
				print_err("lrzip.conf hash value (%d) out of bounds. Please check\n", control->hash_code);
				control->hash_code = 0;
				control->flags &= ~FLAG_HASHED;
			}
		}
		else if (isparameter(parameter, "outputdirectory")) {
			control->outdir = malloc(strlen(parametervalue) + 2);
			if (!control->outdir)
				fatal("Fatal Memory Error in read_config\n");
			strcpy(control->outdir, parametervalue);
			if (strcmp(parametervalue + strlen(parametervalue) - 1, "/"))
				strcat(control->outdir, "/");
		}
		else if (isparameter(parameter,"verbosity")) {
			if (control->flags & FLAG_VERBOSE)
				fatal("CONF.FILE error. Verbosity already defined.\n");
			if (isparameter(parametervalue, "yes"))
				control->flags |= FLAG_VERBOSITY;
			else if (isparameter(parametervalue,"max"))
				control->flags |= FLAG_VERBOSITY_MAX;
			else /* oops, unrecognized value */
				print_err("lrzip.conf: Unrecognized verbosity value %s. Ignored.\n", parametervalue);
		}
		else if (isparameter(parameter, "showprogress")) {
			/* Yes by default */
			if (isparameter(parametervalue, "NO"))
				control->flags &= ~FLAG_SHOW_PROGRESS;
		}
		else if (isparameter(parameter,"nice")) {
			control->nice_val = atoi(parametervalue);
			if (control->nice_val < -20 || control->nice_val > 19)
				fatal("CONF.FILE error. Nice must be between -20 and 19\n");
		}
		else if (isparameter(parameter, "keepbroken")) {
			if (isparameter(parametervalue, "yes" ))
				control->flags |= FLAG_KEEP_BROKEN;
		}
		else if (iscaseparameter(parameter, "DELETEFILES")) {
			/* delete files must be case sensitive */
			if (iscaseparameter(parametervalue, "YES"))
				control->flags &= ~FLAG_KEEP_FILES;
		}
		else if (iscaseparameter(parameter, "REPLACEFILE")) {
			/* replace lrzip file must be case sensitive */
			if (iscaseparameter(parametervalue, "YES"))
				control->flags |= FLAG_FORCE_REPLACE;
		}
		else if (isparameter(parameter, "tmpdir")) {
			control->tmpdir = realloc(NULL, strlen(parametervalue) + 2);
			if (!control->tmpdir)
				fatal("Fatal Memory Error in read_config\n");
			strcpy(control->tmpdir, parametervalue);
			if (strcmp(parametervalue + strlen(parametervalue) - 1, "/"))
				strcat(control->tmpdir, "/");
		}
		else if (isparameter(parameter, "encrypt")) {
			control->enc_code = atoi(parametervalue);
			if (isparameter(parametervalue, "YES") ||
				(control->enc_code > 0 && control->enc_code <= MAXENC))
				control->flags |= FLAG_ENCRYPT;
			else if (control->enc_code > MAXENC) {		// out of bounds
				print_err("lrzip.conf encryption value (%d) out of bounds. Please check.\n", control->enc_code);
				control->enc_code = 0;
				control->flags &= ~FLAG_ENCRYPT;
			}
		}
		else if (isparameter(parameter, "dictionarysize")) {
			int p;
			p = atoi(parametervalue);
			if (p < 0 || p > 40)
				fatal("CONF FILE error. Dictionary Size must be between 0 and 40.\n");
			control->dictSize = ((p == 40) ? 0xFFFFFFFF : ((2 | ((p) & 1)) << ((p) / 2 + 11)));	// Slight modification to lzma2 spec 2^31 OK
		}
		else if (isparameter(parameter, "locale")) {
			if (!isparameter(parametervalue, "DEFAULT")) {
				if (isparameter(parametervalue, "NONE"))
					control->locale = NULL;
				else
					control->locale = strdup(parametervalue);
			}
		}
		else {
			/* oops, we have an invalid parameter, display */
			print_err("lrzip.conf: Unrecognized parameter value, %s = %s. Continuing.\n",\
			       parameter, parametervalue);
		}
	}

	if (unlikely(fclose(fp)))
		fatal("Failed to fclose fp in read_config\n");

/*	fprintf(stderr, "\nWindow = %'d \
		\nCompression Level = %'d \
		\nThreshold = %1.2f \
		\nOutput Directory = %s \
		\nFlags = %'d\n", control->window,control->compression_level, control->threshold, control->outdir, control->flags);
*/
	return true;
}

/* keygen will now use SHAKE128 with an extendable XOF output of keylen */
static void lrz_keygen(const rzip_control *control, const uchar *salt, uchar *key, uchar *iv, int keylen, int ivlen)
{
	uchar *buf;
	buf = calloc(HASH_LEN + SALT_LEN + PASS_LEN, 1);
	gcry_md_hd_t gcry_enchash_handle;
	int algo;

	/* hash size will depend on algo, AES 128 or 256 */
	/* ivlen will alwatys be 16 bytes regardless */
	if (control->enc_code == 1)
		algo = hashes[SHAKE128_16].gcode;
	else
		algo = hashes[SHAKE256_32].gcode;

	mlock(buf, HASH_LEN + SALT_LEN + PASS_LEN);

	memcpy(buf, control->hash, HASH_LEN);
	memcpy(buf + HASH_LEN, salt, SALT_LEN);
	memcpy(buf + HASH_LEN + SALT_LEN, control->salt_pass, control->salt_pass_len);

	/* No error checking for gcrypt key/iv hash functions */
	gcry_md_open(&gcry_enchash_handle, algo, GCRY_MD_FLAG_SECURE);
	gcry_md_write(gcry_enchash_handle, buf, HASH_LEN + SALT_LEN + control->salt_pass_len);
	gcry_md_extract(gcry_enchash_handle, algo, key, keylen);

	gcry_md_reset(gcry_enchash_handle);

	memcpy(buf, key, keylen);
	memcpy(buf + *control->enc_keylen, salt, SALT_LEN);
	memcpy(buf + *control->enc_keylen + SALT_LEN, control->salt_pass, control->salt_pass_len);

	gcry_md_write(gcry_enchash_handle, buf, keylen + SALT_LEN + control->salt_pass_len);
	gcry_md_extract(gcry_enchash_handle, algo, iv, ivlen);
	gcry_md_close(gcry_enchash_handle);

	memset(buf, 0, sizeof(buf));
	munlock(buf, sizeof(buf));
	dealloc(buf);
}

bool lrz_crypt(const rzip_control *control, uchar *buf, i64 len, const uchar *salt, int encrypt)
{
	/* libgcrypt using cipher text stealing simplifies matters */
	uchar *key, *iv;
	gcry_cipher_hd_t gcry_aes_cbc_handle;
	bool ret = false;
	size_t gcry_error;

	/* now allocate as to size of encryption key as
	 * defined in lrzip_private.h */
	key = calloc(*control->enc_keylen, 1);
	iv  = calloc(*control->enc_ivlen, 1);

	/* Generate unique key and IV for each block of data based on salt
	 * HASH_LEN is fixed at 64 */
	mlock(key, *control->enc_keylen);
	mlock(iv,  *control->enc_ivlen);

	lrz_keygen(control, salt, key, iv, *control->enc_keylen, *control->enc_ivlen);

	/* Using libgcrypt using CTS mode to encrypt/decrypt
	 * entrire buffer in one pass. Breaks compatibiity with prior versions.
	 * Error checking may be superfluous, but inserted for clarity and proper coding standard.
	 */
	gcry_error=gcry_cipher_open(&gcry_aes_cbc_handle, *control->enc_gcode, GCRY_CIPHER_MODE_CBC, GCRY_CIPHER_SECURE | GCRY_CIPHER_CBC_CTS);
	if (unlikely(gcry_error))
		fatal("Unable to set AES CBC handle in lrz_crypt: %'d\n", gcry_error);
	gcry_error=gcry_cipher_setkey(gcry_aes_cbc_handle, key, *control->enc_keylen);
	if (unlikely(gcry_error))
		fatal("Failed to set AES CBC key in lrz_crypt: %'d\n", gcry_error);
	gcry_error=gcry_cipher_setiv(gcry_aes_cbc_handle, iv, *control->enc_ivlen);
	if (unlikely(gcry_error))
		fatal("Failed to set AES CBC iv in lrz_crypt: %'d\n", gcry_error);

	if (encrypt == LRZ_ENCRYPT) {
		print_maxverbose("Encrypting data        \n");
		/* Encrypt whole buffer */
		gcry_error=gcry_cipher_encrypt(gcry_aes_cbc_handle, buf, len, NULL, 0);
		if (unlikely(gcry_error))
			fatal("Failed to encrypt AES CBC data in lrz_crypt: %'d\n", gcry_error);
	} else { //LRZ_DECRYPT or LRZ_VALIDATE
		if (encrypt == LRZ_DECRYPT)	// don't print if validating or in info
			print_maxverbose("Decrypting data        \n");
		/* Decrypt whole buffer */
		gcry_error=gcry_cipher_decrypt(gcry_aes_cbc_handle, buf, len, NULL, 0);
		if (unlikely(gcry_error))
				fatal("Failed to decrypt AES CBC data in lrz_crypt: %'d\n", gcry_error);
	}
	gcry_cipher_close(gcry_aes_cbc_handle);

	ret = true;
error:
	memset(iv, 0, *control->enc_ivlen);
	memset(key, 0, *control->enc_keylen);
	munlock(iv, *control->enc_ivlen);
	munlock(key, *control->enc_keylen);
	dealloc(key);
	dealloc(iv);
	return ret;
}

/* now use scrypt for key generation and hashing
 * same code used in some Bitcoins */

void lrz_stretch(rzip_control *control)
{
	gpg_error_t error;
	const int parallelization = 1;
	i64 costfactor;
	int i;

	for (i = 1; i <= 30; i++)
		if ( control->encloops < (1 << i) ) break;

	costfactor = (1 << i-1);

	print_maxverbose("SCRYPTing password: Cost factor %'d, Parallelization Factor: %'d\n", costfactor , parallelization);

	error = gcry_kdf_derive(control->salt_pass, control->salt_pass_len, GCRY_KDF_SCRYPT, costfactor,
			control->salt, SALT_LEN, parallelization,
			HASH_LEN, control->hash);

	if (unlikely(error))
		fatal("Unable to hash password. Not enough memory? Error: %'d\n");
}

/* The block headers are all encrypted so we read the data and salt associated
 * with them, decrypt the data, then return the decrypted version of the
 * values */
bool decrypt_header(rzip_control *control, uchar *head, uchar *c_type,
			   i64 *c_len, i64 *u_len, i64 *last_head, int dec_or_validate)
{
	uchar *buf = head + SALT_LEN;

	memcpy(buf, c_type, 1);
	memcpy(buf + 1, c_len, 8);
	memcpy(buf + 9, u_len, 8);
	memcpy(buf + 17, last_head, 8);

	if (unlikely(!lrz_decrypt(control, buf, 25, head, dec_or_validate)))
		return false;

	memcpy(c_type, buf, 1);
	memcpy(c_len, buf + 1, 8);
	memcpy(u_len, buf + 9, 8);
	memcpy(last_head, buf + 17, 8);
	return true;
}
