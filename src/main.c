/*
   Copyright (C) 2006-2016, 2021 Con Kolivas
   Copyright (C) 2011, 2019, 2020, 2021 Peter Hyman
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
/* lrzip compression - main program */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <signal.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
# include <sys/resource.h>
#endif
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif

#include <termios.h>
#ifdef HAVE_ENDIAN_H
# include <endian.h>
#elif HAVE_SYS_ENDIAN_H
# include <sys/endian.h>
#endif
#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif

#include <dirent.h>
#include <getopt.h>
#include <libgen.h>

#include "rzip.h"
#include "lrzip_core.h"
#include "util.h"
#include "stream.h"
#include <inttypes.h>

/* needed for CRC routines */
#include "7zCrc.h"

#define MAX_PATH_LEN 4096

// progress flag
bool progress_flag=false;

static rzip_control base_control, local_control, *control;

static void usage(bool compat)
{
	print_output("lrz%s version %s\n", compat ? "" : "ip-next", PACKAGE_VERSION);
	print_output("Copyright (C) Con Kolivas 2006-2021\n");
	print_output("Copyright (C) Peter Hyman 2007-2021\n");
	print_output("Based on rzip ");
	print_output("Copyright (C) Andrew Tridgell 1998-2003\n\n");
	print_output("Usage: lrz%s [options] <file...>\n", compat ? "" : "ip");
	print_output("Compression Options:\n--------------------\n");
	print_output("	--lzma			lzma compression (default)\n");
	print_output("	-b, --bzip2		bzip2 compression\n");
	print_output("	-g, --gzip		gzip compression using zlib\n");
	print_output("	-l, --lzo		lzo compression (ultra fast)\n");
	print_output("	-n, --no-compress	no backend compression - prepare for other compressor\n");
	print_output("	-z, --zpaq		zpaq compression (best, extreme compression, extremely slow)\n");
	if (compat) {
		print_output("	-1 .. -9		set lzma/bzip2/gzip compression level (1-9, default 7)\n");
		print_output("	--fast			alias for -1\n");
		print_output("	--best			alias for -9\n");
	}
	if (!compat)
		print_output("	-L, --level level	set lzma/bzip2/gzip compression level (1-9, default 7)\n");
	print_output("	--dictsize		Set lzma Dictionary Size for LZMA ds=0 to 40 expressed as 2<<11, 3<<11, 2<<12, 3<<12...2<<31-1\n");
	print_output("    Filtering Options:\n");
	print_output("	--x86			Use x86 filter (for all compression modes)\n");
	print_output("	--arm			Use ARM filter (for all compression modes)\n");
	print_output("	--armt			Use ARMT filter (for all compression modes)\n");
	print_output("	--ppc			Use PPC filter (for all compression modes)\n");
	print_output("	--sparc			Use SPARC filter (for all compression modes)\n");
	print_output("	--ia64			Use IA64 filter (for all compression modes)\n");
	print_output("	--delta	[1..32]		Use Delta filter (for all compression modes) (1 (default) -17, then multiples of 16 to 256)\n");
	print_output("    Additional Compression Options:\n");
	if (compat)
		print_output("	-c, --stdout		output to STDOUT\n");
	print_output("	-e, --encrypt[=password] password protected sha512/aes128 encryption on compression\n");
	if (!compat)
		print_output("	-D, --delete		delete existing files\n");
	print_output("	-f, --force		force overwrite of any existing files\n");
	if (compat)
		print_output("	-k, --keep		don't delete source files on de/compression\n");
	print_output("	-K, --keep-broken	keep broken or damaged output files\n");
	print_output("	-o, --outfile filename	specify the output file name and/or path\n");
	print_output("	-O, --outdir directory	specify the output directory when -o is not used\n");
	print_output("	-S, --suffix suffix	specify compressed suffix (default '.lrz')\n");
	print_output("    Low level Compression Options:\n");
	print_output("	-N, --nice-level value	Set nice value to value (default %d)\n", compat ? 0 : 19);
	print_output("	-m, --maxram size	Set maximum available ram in hundreds of MB\n\t\t\t\tOverrides detected amount of available ram. \
Useful for testing\n");
	print_output("	-R, --rzip-level level	Set independent RZIP Compression Level (1-9) for pre-processing (default=compression level)\n");
	print_output("	-T, --threshold [limit]	Disable LZ4 compressibility testing OR set limit to determine compressibiity (1-99)\n\t\t\t\t\
Note: Since limit is optional, the short option must not have a space. e.g. -T75, not -T 75\n");
	print_output("	-U, --unlimited		Use unlimited window size beyond ramsize (potentially much slower)\n");
	print_output("	-w, --window size	maximum compression window in hundreds of MB\n\t\t\t\t\
default chosen by heuristic dependent on ram and chosen compression\n");
	print_output("Decompression Options:\n----------------------\n");
	print_output("	-d, --decompress	decompress\n");
	print_output("  -e, -f -o -O		Same a Compression Options\n");
	print_output("	-t, --test		test compressed file integrity\n");
	if (compat)
		print_output("	-C, --check		check integrity of file written on decompression\n");
	else
		print_output("	-c, -C, --check		check integrity of file written on decompression\n");
	print_output("General Options:\n----------------\n");
	print_output("	-h, -?, --help		show help\n");
	print_output("	-H, --hash		display md5 hash integrity information\n");
	print_output("	-i, --info		show compressed file information\n");
	if (compat) {
		print_output("	-L, --license		display software version and license\n");
		print_output("	-P, --progress		show compression progress\n");
	} else
		print_output("	-q, --quiet		don't show compression progress\n");
	print_output("	-p, --threads value	Set processor count to override number of threads\n");
	print_output("	-r, --recursive		operate recursively on directories\n");
	print_output("	-v[v%s], --verbose	Increase verbosity\n", compat ? "v" : "");
	print_output("	-V, --version		display software version and license\n");
	print_output("\nLRZIP=NOCONFIG environment variable setting can be used to bypass lrzip.conf.\n\
TMP environment variable will be used for storage of temporary files when needed.\n\
TMPDIR may also be stored in lrzip.conf file.\n\
\nIf no filenames or \"-\" is specified, stdin/out will be used.\n");

}

static void license(bool compat)
{
	print_output("lrz%s version %s\n\
Copyright (C) Con Kolivas 2006-2016\n\
Copyright (C) Peter Hyman 2007-2021\n\
Based on rzip Copyright (C) Andrew Tridgell 1998-2003\n\n\
This is free software.  You may redistribute copies of it under the terms of\n\
the GNU General Public License <http://www.gnu.org/licenses/gpl.html>.\n\
There is NO WARRANTY, to the extent permitted by law.\n",
compat ? "" : "ip", PACKAGE_VERSION);
}

static void sighandler(int sig __UNUSED__)
{
	signal(sig, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	print_err("Interrupted\n");
	fatal_exit(&local_control);
}

static void show_summary(void)
{
	/* OK, if verbosity set, print summary of options selected */
	if (!INFO) {
		print_verbose("The following options are in effect for this %s.\n",
			      DECOMPRESS ? "DECOMPRESSION" : TEST_ONLY ? "INTEGRITY TEST" : "COMPRESSION");
		print_verbose("Threading is %s. Number of CPUs detected: %d\n", control->threads > 1? "ENABLED" : "DISABLED",
			      control->threads);
		print_verbose("Detected %lld bytes ram\n", control->ramsize);
		print_verbose("Nice Value: %d\n", control->nice_val);
		print_verbose("Show Progress\n");
		print_maxverbose("Max ");
		print_verbose("Verbose\n");
		if (FORCE_REPLACE)
			print_verbose("Overwrite Files\n");
		if (!KEEP_FILES)
			print_verbose("Remove input files on completion\n");
		if (control->outdir)
			print_verbose("Output Directory Specified: %s\n", control->outdir);
		else if (control->outname)
			print_verbose("Output Filename Specified: %s\n", control->outname);
		if (TEST_ONLY)
			print_verbose("Test file integrity\n");
		if (control->tmpdir)
			print_verbose("Temporary Directory set as: %s\n", control->tmpdir);

		/* show compression options */
		if (!DECOMPRESS && !TEST_ONLY) {
			print_verbose("Compression mode is: %s",
					(LZMA_COMPRESS ? "LZMA" :
					(LZO_COMPRESS ? "LZO\n" :	// No Threshold testing
					(BZIP2_COMPRESS ? "BZIP2" :
					(ZLIB_COMPRESS ? "GZIP\n" :	// No Threshold testing
					(ZPAQ_COMPRESS ? "ZPAQ" :
					(NO_COMPRESS ? "RZIP pre-processing only" : "wtf")))))));
			if (!LZO_COMPRESS && !ZLIB_COMPRESS)
				print_verbose(". LZ4 Compressibility testing %s\n", (LZ4_TEST? "enabled" : "disabled"));
			if (LZ4_TEST && control->threshold != 100)
				print_verbose("Threshhold limit = %d\%\n", control->threshold);
			print_verbose("Compression level %d\n", control->compression_level);
			print_verbose("RZIP Compression level %d\n", control->rzip_compression_level);
			if (LZMA_COMPRESS)
				print_verbose("Initial LZMA Dictionary Size: %"PRIu32"\n", control->dictSize );
			if (ZPAQ_COMPRESS)
				print_verbose("ZPAQ Compression Level: %d, ZPAQ initial Block Size: %d\n",
					       control->zpaq_level, control->zpaq_bs);
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
					print_output(", offset - %d", control->delta);
				print_output("\n");
			}
			if (control->window)
				print_verbose("Compression Window: %lld = %lldMB\n", control->window, control->window * 100ull);
			/* show heuristically computed window size */
			if (!control->window && !UNLIMITED) {
				i64 temp_chunk, temp_window;

				if (STDOUT || STDIN)
					temp_chunk = control->maxram;
				else
					temp_chunk = control->ramsize * 2 / 3;
				temp_window = temp_chunk / (100 * 1024 * 1024);
				print_verbose("Heuristically Computed Compression Window: %lld = %lldMB\n", temp_window, temp_window * 100ull);
			}
			if (UNLIMITED)
				print_verbose("Using Unlimited Window size\n");
			print_maxverbose("Storage time in seconds %lld\n", control->secs);
		}
		if (ENCRYPT)
			print_maxverbose("Encryption hash loops %lld\n", control->encloops);
	}
}

static struct option long_options[] = {
	{"bzip2",	no_argument,	0,	'b'},		/* 0 */
	{"check",	no_argument,	0,	'c'},
	{"check",	no_argument,	0,	'C'},
	{"decompress",	no_argument,	0,	'd'},
	{"delete",	no_argument,	0,	'D'},
	{"encrypt",	optional_argument,	0,	'e'},	/* 5 */
	{"force",	no_argument,	0,	'f'},
	{"gzip",	no_argument,	0,	'g'},
	{"help",	no_argument,	0,	'h'},
	{"hash",	no_argument,	0,	'H'},
	{"info",	no_argument,	0,	'i'},		/* 10 */
	{"keep-broken",	no_argument,	0,	'k'},
	{"keep-broken",	no_argument,	0,	'K'},
	{"lzo",		no_argument,	0,	'l'},
	{"lzma",       	no_argument,	0,	0},
	{"level",	optional_argument,	0,	'L'},	/* 15 */
	{"license",	no_argument,	0,	'L'},
	{"maxram",	required_argument,	0,	'm'},
	{"no-compress",	no_argument,	0,	'n'},
	{"nice-level",	required_argument,	0,	'N'},
	{"outfile",	required_argument,	0,	'o'},	/* 20 */
	{"outdir",	required_argument,	0,	'O'},
	{"threads",	required_argument,	0,	'p'},
	{"progress",	no_argument,	0,	'P'},
	{"quiet",	no_argument,	0,	'q'},
	{"recursive",	no_argument,	0,	'r'},		/* 25 */
	{"suffix",	required_argument,	0,	'S'},
	{"test",	no_argument,	0,	't'},
	{"threshold",	optional_argument,	0,	'T'},
	{"unlimited",	no_argument,	0,	'U'},
	{"verbose",	no_argument,	0,	'v'},		/* 30 */
	{"version",	no_argument,	0,	'V'},
	{"window",	required_argument,	0,	'w'},
	{"zpaq",	no_argument,	0,	'z'},
	{"fast",	no_argument,	0,	'1'},
	{"best",	no_argument,	0,	'9'},		/* 35 */
	{"dictsize",	required_argument,	0,	0},
	{"x86",		no_argument,	0,	0},
	{"arm",		no_argument,	0,	0},
	{"armt",	no_argument,	0,	0},
	{"ppc",		no_argument,	0,	0},		/* 40 */
	{"sparc",	no_argument,	0,	0},
	{"ia64",	no_argument,	0,	0},
	{"delta",	optional_argument,	0,	0},
	{"rzip-level",	required_argument,	0,	'R'},
	{0,	0,	0,	0},
};

static void set_stdout(struct rzip_control *control)
{
	control->flags |= FLAG_STDOUT;
	control->outFILE = stdout;
	control->msgout = stderr;
	register_outputfile(control, control->msgout);
}

/* Recursively enter all directories, adding all regular files to the dirlist array */
static void recurse_dirlist(char *indir, char **dirlist, int *entries)
{
	char fname[MAX_PATH_LEN];
	struct stat istat;
	struct dirent *dp;
	DIR *dirp;

	dirp = opendir(indir);
	if (unlikely(!dirp))
		failure("Unable to open directory %s\n", indir);
	while ((dp = readdir(dirp)) != NULL) {
		if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
			continue;
		sprintf(fname, "%s/%s", indir, dp->d_name);
		if (unlikely(stat(fname, &istat)))
			failure("Unable to stat file %s\n", fname);
		if (S_ISDIR(istat.st_mode)) {
			recurse_dirlist(fname, dirlist, entries);
			continue;
		}
		if (!S_ISREG(istat.st_mode)) {
			print_err("Not regular file %s\n", fname);
			continue;
		}
		print_maxverbose("Added file %s\n", fname);
		*dirlist = realloc(*dirlist, MAX_PATH_LEN * (*entries + 1));
		strcpy(*dirlist + MAX_PATH_LEN * (*entries)++, fname);
	}
	closedir(dirp);
}

static const char *loptions = "bcCdDe::fghHiKlL:nN:o:O:p:PqrR:S:tT::Um:vVw:z?";
static const char *coptions = "bcCde::fghHikKlLnN:o:O:p:PrR:S:tT::Um:vVw:z?123456789";

int main(int argc, char *argv[])
{
	bool lrzcat = false, compat = false, recurse = false;
	bool options_file = false, conf_file_compression_set = false; /* for environment and tracking of compression setting */
	struct timeval start_time, end_time;
	struct sigaction handler;
	double seconds,total_time; // for timers
	bool nice_set = false;
	int c, i, ds, long_opt_index;
	int hours,minutes;
	extern int optind;
	char *eptr, *av; /* for environment */
	char *endptr = NULL;

        control = &base_control;

	initialise_control(control);

	av = basename(argv[0]);
	if (!strcmp(av, "lrunzip"))
		control->flags |= FLAG_DECOMPRESS;
	else if (!strcmp(av, "lrzcat")) {
		control->flags |= FLAG_DECOMPRESS | FLAG_STDOUT;
		lrzcat = true;
	} else if (!strcmp(av, "lrz")) {
		/* Called in gzip compatible command line mode */
		control->flags &= ~FLAG_SHOW_PROGRESS;
		control->flags &= ~FLAG_KEEP_FILES;
		compat = true;
		long_options[1].name = "stdout";
		long_options[11].name = "keep";
	}

	/* generate crc table */
	CrcGenerateTable();

	/* Get Preloaded Defaults from lrzip.conf
	 * Look in ., $HOME/.lrzip/, /etc/lrzip.
	 * If LRZIP=NOCONFIG is set, then ignore config
	 * If lrzip.conf sets a compression mode, options_file will be true.
	 * This will allow for a test to permit an override of compression mode.
	 * If there is an override, then all compression settings will be reset
	 * and command line switches will prevail, including for --lzma.
	 */
	eptr = getenv("LRZIP");
	if (eptr == NULL)
		options_file = read_config(control);
	else if (!strstr(eptr,"NOCONFIG"))
		options_file = read_config(control);
	if (options_file && (control->flags & FLAG_NOT_LZMA))		/* if some compression set in lrzip.conf    */
		conf_file_compression_set = true;			/* need this to allow command line override */

	while ((c = getopt_long(argc, argv, compat ? coptions : loptions, long_options, &long_opt_index)) != -1) {
		switch (c) {
		case 'b':
		case 'g':
		case 'l':
		case 'n':
		case 'z':
			/* If some compression was chosen in lrzip.conf, allow this one time
			 * because conf_file_compression_set will be true
			 */
			if ((control->flags & FLAG_NOT_LZMA) && conf_file_compression_set == false)
				failure("Can only use one of -l, -b, -g, -z or -n\n");
			/* Select Compression Mode */
			control->flags &= ~FLAG_NOT_LZMA; 		/* must clear all compressions first */
			if (c == 'b')
				control->flags |= FLAG_BZIP2_COMPRESS;
			else if (c == 'g')
				control->flags |= FLAG_ZLIB_COMPRESS;
			else if (c == 'l')
				control->flags |= FLAG_LZO_COMPRESS;
			else if (c == 'n')
				control->flags |= FLAG_NO_COMPRESS;
			else if (c == 'z')
				control->flags |= FLAG_ZPAQ_COMPRESS;
			/* now FLAG_NOT_LZMA will evaluate as true */
			conf_file_compression_set = false;
			break;
		case 'c':
			if (compat) {
				control->flags |= FLAG_KEEP_FILES;
				set_stdout(control);
				break;
			}
			/* FALLTHRU */
		case 'C':
			control->flags |= FLAG_CHECK;
			control->flags |= FLAG_HASH;
			break;
		case 'd':
			control->flags |= FLAG_DECOMPRESS;
			break;
		case 'D':
			control->flags &= ~FLAG_KEEP_FILES;
			break;
		case 'e':
			control->flags |= FLAG_ENCRYPT;
			if (optarg)
				control->passphrase = optarg;
			break;
		case 'f':
			control->flags |= FLAG_FORCE_REPLACE;
			break;
		case 'h':
		case '?':
			usage(compat);
			return -1;
		case 'H':
			control->flags |= FLAG_HASH;
			break;
		case 'i':
			control->flags |= FLAG_INFO;
			control->flags &= ~FLAG_DECOMPRESS;
			break;
		case 'k':
			if (compat) {
				control->flags |= FLAG_KEEP_FILES;
				break;
			}
			/* FALLTHRU */
		case 'K':
			control->flags |= FLAG_KEEP_BROKEN;
			break;
		case 'L':
			if (compat) {
				license(compat);
				exit(0);
			}
			control->compression_level = strtol(optarg, &endptr, 10);
			if (*endptr)
				failure("Extra characters after compression level: \'%s\'\n", endptr);
			if (control->compression_level < 1 || control->compression_level > 9)
				failure("Invalid compression level (must be 1-9)\n");
			break;
		case 'R':
			/* explicitly set rzip compression level */
			control->rzip_compression_level = strtol(optarg, &endptr, 10);
			if (*endptr)
				failure("Extra characters after rzip compression level: \'%s\'\n", endptr);
			if (control->rzip_compression_level < 1 || control->rzip_compression_level > 9)
				failure("Invalid rzip compression level (must be 1-9)\n");
			break;
		case 'm':
			control->ramsize = strtol(optarg, &endptr, 10) * 1024 * 1024 * 100;
			if (*endptr)
				failure("Extra characters after ramsize: \'%s\'\n", endptr);
			break;
		case 'N':
			nice_set = true;
			control->nice_val = strtol(optarg, &endptr, 10);
			if (*endptr)
				failure("Extra characters after nice level: \'%s\'\n", endptr);
			if (control->nice_val < PRIO_MIN || control->nice_val > PRIO_MAX)
				failure("Invalid nice value (must be %d...%d)\n", PRIO_MIN, PRIO_MAX);
			break;
		case 'o':
			if (control->outdir)
				failure("Cannot have -o and -O together\n");
			if (unlikely(STDOUT))
				failure("Cannot specify an output filename when outputting to stdout\n");
			control->outname = optarg;
			control->suffix = "";
			break;
		case 'O':
			if (control->outname)	/* can't mix -o and -O */
				failure("Cannot have options -o and -O together\n");
			if (unlikely(STDOUT))
				failure("Cannot specify an output directory when outputting to stdout\n");
			control->outdir = malloc(strlen(optarg) + 2);
			if (control->outdir == NULL)
				fatal("Failed to allocate for outdir\n");
			strcpy(control->outdir,optarg);
			if (strcmp(optarg+strlen(optarg) - 1, "/")) 	/* need a trailing slash */
				strcat(control->outdir, "/");
			break;
		case 'p':
			control->threads = strtol(optarg, &endptr, 10);
			if (*endptr)
				failure("Extra characters after number of threads: \'%s\'\n", endptr);
			if (control->threads < 1)
				failure("Must have at least one thread\n");
			break;
		case 'P':
			control->flags |= FLAG_SHOW_PROGRESS;
			break;
		case 'q':
			control->flags &= ~FLAG_SHOW_PROGRESS;
			break;
		case 'r':
			recurse = true;
			break;
		case 'S':
			if (control->outname)
				failure("Specified output filename already, can't specify an extension.\n");
			if (unlikely(STDOUT))
				failure("Cannot specify a filename suffix when outputting to stdout\n");
			control->suffix = optarg;
			break;
		case 't':
			if (control->outname)
				failure("Cannot specify an output file name when just testing.\n");
			if (compat)
				control->flags |= FLAG_KEEP_FILES;
			if (!KEEP_FILES)
				failure("Doubt that you want to delete a file when just testing.\n");
			control->flags |= FLAG_TEST_ONLY;
			break;
		case 'T':
			/* process optional threshold limit
			 * or disable threshold testing
			 */
			if (optarg) {
				i=strtol(optarg, &endptr, 10);
				if (*endptr)
					failure("Extra characters after threshold limit: \'%s\'\n", endptr);
				if (i < 1 || i > 99)
					failure("Threshhold limits are 1-99\n");
				control->threshold = i;
			} else
				control->flags &= ~FLAG_THRESHOLD;
			break;
		case 'U':
			control->flags |= FLAG_UNLIMITED;
			break;
		case 'v':
			/* set verbosity flag */
			if (!(control->flags & FLAG_SHOW_PROGRESS))
				control->flags |= FLAG_SHOW_PROGRESS;
			else if (!(control->flags & FLAG_VERBOSITY) && !(control->flags & FLAG_VERBOSITY_MAX))
				control->flags |= FLAG_VERBOSITY;
			else if ((control->flags & FLAG_VERBOSITY)) {
				control->flags &= ~FLAG_VERBOSITY;
				control->flags |= FLAG_VERBOSITY_MAX;
			}
			break;
		case 'V':
			license(compat);
			exit(0);
			break;
		case 'w':
			control->window = strtol(optarg, &endptr, 10);
			if (*endptr)
				failure("Extra characters after window size: \'%s\'\n", endptr);
			if (control->window < 1)
				failure("Window must be positive\n");
			break;
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			control->compression_level = c - '0';
			break;
		case 0:	/* these are long options without a short code */
			if (FILTER_USED && long_opt_index >= 37 )
				print_output("Filter already selected. %s filter ignored.\n", long_options[long_opt_index].name);
			else {
				switch(long_opt_index) {
					/* in case lzma selected, need to reset not lzma flag */
					case 14:
						control->flags &= ~FLAG_NOT_LZMA;		/* clear alternate compression flags */
						break;
					case 36:
						/* Dictionary Size,	2<<11, 3<<11
						 * 			2<<12, 3<<12
						 * 			...
						 * 			2<<30, 3<<30
						 * 			2<<31 - 1
						 * Uses new lzma2 limited dictionary sizes */
						if (!LZMA_COMPRESS)
							print_err("--dictsize option only valid for LZMA compression. Ignorred.\n");
						ds = strtol(optarg, &endptr, 10);
						if (*endptr)
							failure("Extra characters after dictionary size: \'%s\'\n", endptr);
						if (ds< 0 || ds > 40)
							failure("Dictionary Size must be between 0 and 40 for 2^12 (4KB) to 2^31 (4GB)");
						control->dictSize = ((ds == 40) ? 0xFFFFFFFF : (((u32)2 | (ds & 1)) << (ds / 2 + 11)));	// Slight modification to lzma2 spec 2^31 OK
						break;
						/* Filtering */
					case 37:
						control->filter_flag = FILTER_FLAG_X86;		// x86
						break;
					case 38:
						control->filter_flag = FILTER_FLAG_ARM;		// ARM
						break;
					case 39:
						control->filter_flag = FILTER_FLAG_ARMT;	// ARMT
						break;
					case 40:
						control->filter_flag = FILTER_FLAG_PPC;		// PPC
						break;
					case 41:
						control->filter_flag = FILTER_FLAG_SPARC;	// SPARC
						break;
					case 42:
						control->filter_flag = FILTER_FLAG_IA64;	// IA64
						break;
					case 43:
						control->filter_flag = FILTER_FLAG_DELTA;	// DELTA
						/* Delta Values are 1-16, then multiples of 16 to 256 */
						if (optarg) {
							i=strtol(optarg, &endptr, 10);
							if (*endptr)
								failure("Extra characters after delta offset: \'%s\'\n", endptr);
							if (i < 1 || i > 32)
								failure("Delta offset value must be between 1 and 32\n");
							control->delta = ( i <= 17 ? i : (i-16) * 16 );
						} else
							control->delta = DEFAULT_DELTA;		// 1 is default
						break;
				}	//switch
			}	//if filter used
		}	// main switch
	}	// while input loop

	argc -= optind;
	argv += optind;

	/* if rzip compression level not set, make equal to compression level */
	if (! control->rzip_compression_level )
		control->rzip_compression_level = control->compression_level;

	if (control->outname) {
		if (argc > 1)
			failure("Cannot specify output filename with more than 1 file\n");
		if (recurse)
			failure("Cannot specify output filename with recursive\n");
	}

	if (VERBOSE && !SHOW_PROGRESS) {
		print_err("Cannot have -v and -q options. -v wins.\n");
		control->flags |= FLAG_SHOW_PROGRESS;
	}

	if (UNLIMITED && control->window) {
		print_err("If -U used, cannot specify a window size with -w.\n");
		control->window = 0;
	}

	if (argc < 1)
		control->flags |= FLAG_STDIN;

	if (UNLIMITED && STDIN) {
		print_err("Cannot have -U and stdin, unlimited mode disabled.\n");
		control->flags &= ~FLAG_UNLIMITED;
	}

	/* if any filter used, disable LZ4 testing or certain compression modes */
	if ((control->flags & FLAG_THRESHOLD) && (FILTER_USED || ZLIB_COMPRESS || LZO_COMPRESS || NO_COMPRESS)) {
		print_output("LZ4 Threshold testing disabled due to Filtering and/or Compression type (gzip, lzo, rzip).\n");
		control->flags &= ~FLAG_THRESHOLD;
	}

	setup_overhead(control);

	/* Set the main nice value to half that of the backend threads since
	 * the rzip stage is usually the rate limiting step */
	control->current_priority = getpriority(PRIO_PROCESS, 0);
	if (nice_set) {
		if (!NO_COMPRESS) {
			/* If niceness can't be set. just reset process priority */
			if (unlikely(setpriority(PRIO_PROCESS, 0, control->nice_val/2) == -1)) {
				print_err("Warning, unable to set nice value %d...Resetting to %d\n",
					control->nice_val, control->current_priority);
				setpriority(PRIO_PROCESS, 0, (control->nice_val=control->current_priority));
			}
		} else {
			if (unlikely(setpriority(PRIO_PROCESS, 0, control->nice_val) == -1)) {
				print_err("Warning, unable to set nice value %d...Resetting to %d\n",
					control->nice_val, control->current_priority);
				setpriority(PRIO_PROCESS, 0, (control->nice_val=control->current_priority));
			}
		}
	}

	/* One extra iteration for the case of no parameters means we will default to stdin/out */
	for (i = 0; i <= argc; i++) {
		char *dirlist = NULL, *infile = NULL;
		int direntries = 0, curentry = 0;

		if (i < argc)
			infile = argv[i];
		else if (!(i == 0 && STDIN))
			break;
		if (infile && !(DECOMPRESS || INFO || TEST_ONLY)) {
			/* check that input file exists, unless Decompressing or Test */
			if ((strcmp(infile, "-") == 0))
				control->flags |= FLAG_STDIN;
			else {
				bool isdir = false;
				struct stat istat;

				if (unlikely(stat(infile, &istat)))
					failure("Failed to stat %s\n", infile);
				isdir = S_ISDIR(istat.st_mode);
				if (!recurse && (isdir || !S_ISREG(istat.st_mode))) {
					failure("lrzip only works directly on regular FILES.\n"
					"Use -r recursive, lrztar or pipe through tar for compressing directories.\n");
				}
				if (recurse && !isdir)
					failure("%s not a directory, -r recursive needs a directory\n", infile);
			}
		}

		if (recurse) {
			if (unlikely(STDIN || STDOUT))
				failure("Cannot use -r recursive with STDIO\n");
			recurse_dirlist(infile, &dirlist, &direntries);
		}

		if (INFO && STDIN)
			failure("Will not get file info from STDIN\n");
recursion:
		if (recurse) {
			if (curentry >= direntries) {
				infile = NULL;
				continue;
			}
			infile = dirlist + MAX_PATH_LEN * curentry++;
		}
		control->infile = infile;

		/* If no output filename is specified, and we're using
		 * stdin, use stdout */
		if ((control->outname && (strcmp(control->outname, "-") == 0)) ||
		    (!control->outname && STDIN) || lrzcat)
				set_stdout(control);

		if (lrzcat) {
			control->msgout = stderr;
			control->outFILE = stdout;
			register_outputfile(control, control->msgout);
		}

		if (!STDOUT) {
			control->msgout = stdout;
			register_outputfile(control, control->msgout);
		}

		if (STDIN)
			control->inFILE = stdin;

		/* Implement signal handler only once flags are set */
		sigemptyset(&handler.sa_mask);
		handler.sa_flags = 0;
		handler.sa_handler = &sighandler;
		sigaction(SIGTERM, &handler, 0);
		sigaction(SIGINT, &handler, 0);

		if (!FORCE_REPLACE) {
			if (STDIN && isatty(fileno((FILE *)stdin))) {
				print_err("Will not read stdin from a terminal. Use -f to override.\n");
				usage(compat);
				exit (1);
			}
			if (!TEST_ONLY && STDOUT && isatty(fileno((FILE *)stdout)) && !compat) {
				print_err("Will not write stdout to a terminal. Use -f to override.\n");
				usage(compat);
				exit (1);
			}
		}

		if (CHECK_FILE) {
			if (!DECOMPRESS) {
				print_err("Can only check file written on decompression.\n");
				control->flags &= ~FLAG_CHECK;
			} else if (STDOUT) {
				print_err("Can't check file written when writing to stdout. Checking disabled.\n");
				control->flags &= ~FLAG_CHECK;
			}
		}

		setup_ram(control);
		show_summary();

		gettimeofday(&start_time, NULL);

		if (unlikely(STDIN && ENCRYPT && control->passphrase == NULL))
			failure("Unable to work from STDIN while reading password. Use -e passphrase.\n");
		if (unlikely(STDOUT && !(DECOMPRESS || INFO || TEST_ONLY) && ENCRYPT))
			failure("Unable to encrypt while writing to STDOUT.\n");

		memcpy(&local_control, &base_control, sizeof(rzip_control));
		if (DECOMPRESS || TEST_ONLY)
			decompress_file(&local_control);
		else if (INFO)
			get_fileinfo(&local_control);
		else
			compress_file(&local_control);

		/* compute total time */
		gettimeofday(&end_time, NULL);
		total_time = (end_time.tv_sec + (double)end_time.tv_usec / 1000000) -
			      (start_time.tv_sec + (double)start_time.tv_usec / 1000000);
		hours = (int)total_time / 3600;
		minutes = (int)(total_time / 60) % 60;
		seconds = total_time - hours * 3600 - minutes * 60;
		if (!INFO)
			print_progress("Total time: %02d:%02d:%05.2f\n", hours, minutes, seconds);
		if (recurse)
			goto recursion;
	}

	return 0;
}
