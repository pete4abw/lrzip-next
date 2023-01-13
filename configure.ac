##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
m4_define([v_maj], [m4_esyscmd_s([./util/gitdesc.sh major])])
m4_define([v_min], [m4_esyscmd_s([./util/gitdesc.sh minor])])
m4_define([v_mic], [m4_esyscmd_s([./util/gitdesc.sh micro])])
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
m4_define([v_ver], [v_maj.v_min.v_mic])
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
dnl Process this file with autoconf to produce a configure script.
AC_INIT([lrzip-next],[v_ver],[pete@peterhyman.com])
AC_PREREQ([2.59])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.6 dist-bzip2 foreign subdir-objects])
AC_USE_SYSTEM_EXTENSIONS

LT_INIT

##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
m4_ifdef([v_rev], , [m4_define([v_rev], [0])])
m4_ifdef([v_rel], , [m4_define([v_rel], [])])
AC_DEFINE_UNQUOTED(LRZIP_MAJOR_VERSION, [v_maj], [Major version])
AC_DEFINE_UNQUOTED(LRZIP_MINOR_VERSION, [v_min], [Minor version])
AC_DEFINE_UNQUOTED(LRZIP_MINOR_SUBVERSION, [v_mic], [Micro version])
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
VMAJ=v_maj
AC_SUBST(VMAJ)

AC_CANONICAL_BUILD
AC_CANONICAL_HOST

dnl Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_LN_S
AC_SUBST(SHELL)
AC_SYS_LARGEFILE
AC_FUNC_FSEEKO
AC_FUNC_ALLOCA

AS_IF([test x"$ac_cv_prog_cc_c99" = xno],
	AC_MSG_ERROR([C compiler does not support C99]))

AC_CHECK_SIZEOF(short)
AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(long)

## no more 32 bit CPUs
AS_IF([test $ac_cv_sizeof_long = 4],
      AC_MSG_ERROR([Size of long type is 4 bytes. 32 bit processors are NO LONGER supported in lrzip-next]))

AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_C___ATTRIBUTE__
AC_C_INLINE
AC_C_BIGENDIAN

static=no
AC_ARG_ENABLE([static-bin],
	[AS_HELP_STRING([--enable-static-bin],[Build statically linked binary @<:@default=no@:>@])],
	[static=$enableval]
	)
AM_CONDITIONAL([STATIC], [test x"$static" = xyes])

debug=no
AC_ARG_ENABLE([debug],
	[AS_HELP_STRING([--enable-debug],[Compile with -Og flag for debugger @<:@default=no@:>@])],
	[debug=$enableval]
	)
AM_CONDITIONAL([DEBUG], [test x"$debug" = xyes])

AC_ARG_ENABLE(
		asm,
		[AS_HELP_STRING([--enable-asm],[Enable native Assembly code])],
		ASM=$enableval,
		ASM=yes
	)

ASM_PROG=
ASM_OPT=
if test x"$ASM" = xyes; then
	AC_CHECK_PROGS( ASM_PROG, nasm yasm, no,  ) # Allow test for multiple Assemblers.
	if test x"$ASM_PROG" = xno; then
		ASM=no
		ASM_PROG=
	else
## only x86_64
		case $host in
			x86_64-*)
				ASM_OPT="-Dx64 -f elf64";
				case "$ASM_PROG" in
					nasm)
						AS_IF([test x"$debug" = xyes],
						      ASM_OPT+=" -g -F dwarf")
						;;
					yasm)
						AS_IF([test x"$debug" = xyes]
						      ASM_OPT+=" -g dwarf2")
						;;
					*)
						# should never get here, but in case...
						AC_MSG_ERROR([Internal Error: No Assembler found!])
						;;
				esac
				;;
			*)
				ASM=no
				;;
		esac
	fi
fi

AM_CONDITIONAL([USE_ASM], [test x"$ASM" = xyes])
AC_SUBST(ASM_OPT)
AC_SUBST(ASM_PROG)

AC_CHECK_HEADERS(fcntl.h sys/time.h unistd.h sys/mman.h)
AC_CHECK_HEADERS(ctype.h errno.h sys/resource.h)
AC_CHECK_HEADERS(endian.h sys/endian.h arpa/inet.h)
AC_CHECK_HEADERS(alloca.h pthread.h)
AC_CHECK_HEADERS(gcrypt.h gpg-error.h)

# remove rzip large file check. From version 0.24!

AC_CHECK_LIB(pthread, pthread_create, ,
	AC_MSG_ERROR([Could not find pthread library - please install libpthread]))
AC_CHECK_LIB(m, sqrt, ,
	AC_MSG_ERROR([Could not find math library - please install libm]))
AC_CHECK_LIB(z, compress2, ,
	AC_MSG_ERROR([Could not find zlib library - please install zlib-dev]))
AC_CHECK_LIB(bz2, BZ2_bzBuffToBuffCompress, ,
        AC_MSG_ERROR([Could not find bz2 library - please install libbz2-dev]))
AC_CHECK_LIB(lzo2, lzo1x_1_compress, ,
        AC_MSG_ERROR([Could not find lzo2 library - please install liblzo2-dev]))
AC_CHECK_LIB(lz4, LZ4_compress_default, ,
	AC_MSG_ERROR([Could not find lz4 library - please install liblz4-dev]))
AC_CHECK_LIB(gpg-error, gpg_err_code_to_errno, ,
	AC_MSG_ERROR([Could not find gpg-error library - please install libgpg-error-dev]))
AC_CHECK_LIB(gcrypt, gcry_md_open, ,
	AC_MSG_ERROR([Could not find gcrypt library - please install libgcrypt-dev]))
AC_CHECK_FUNCS(mmap strerror)
AC_CHECK_FUNCS(getopt_long)

AX_PTHREAD
LIBS="$PTHREAD_LIBS $LIBS"
AS_IF([test x"$debug" = xyes],
	 CFLAGS="-g -Og"
	 CXXFLAGS="-g -Og",
	 CFLAGS="$CFLAGS"
	 CXXFLAGS="$CXXFLAGS")

CFLAGS+=" $PTHREAD_CFLAGS"
CXXFLAGS+=" $PTHREAD_CFLAGS"

## test for ARM or other non x86 processor
## must use -DNOJIT for compiling zpaq
case $host in
	*86*)
		;;
	*)
		CXXFLAGS+=" -DNOJIT"
		;;
esac

AC_CONFIG_FILES([
Makefile
src/Makefile
src/lzma/Makefile
src/lzma/C/Makefile
src/lib/libzpaq/Makefile
src/lib/libbz3/Makefile
doc/Makefile
man/Makefile
])

AS_IF([test x"$ASM" = xyes],
      AC_CONFIG_FILES([src/lzma/ASM/Makefile]),[echo "Not making ASM Makefile"])

AC_OUTPUT

echo
echo
echo
echo "------------------------------------------------------------------------"
echo "$PACKAGE $VERSION"
echo "------------------------------------------------------------------------"
echo
echo
echo "Configuration Options Summary:"
echo
echo "  Static binary......: $static"
echo "  Host system........: $host"
echo "  Build system.......: $build"
AS_IF([test x"$debug" = xyes], echo "  Debugging mode.....: yes")
echo
echo "Compilation............: make (or gmake)"
echo "  CPPFLAGS.............: $CPPFLAGS"
echo "  CFLAGS...............: $CFLAGS"
echo "  CXXFLAGS.............: $CXXFLAGS"
echo -n "  ASM..................: "
AS_IF([test x"$ASM" != xyes],
	echo "no",
	echo "$ASM[[, ]] using $ASM_PROG Assembler"
	echo "  ASMFLAGS.............: $ASM_OPT")
echo "  LDFLAGS..............: $LDFLAGS"
echo
echo "Installation...........: make install (as root if needed, with 'su' or 'sudo')"
echo "  prefix...............: $prefix"
AS_IF([test x"$static" = xyes],
gver=$(ldd --version | head -n1 | rev | cut -f1 -d\ | rev)
	echo "WARNING: Static Binary will require runtime glibc libraries of the same version used to link it here: $gver. Therefore static binary may not be portable to other systems.")
echo
