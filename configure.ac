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

AC_PROG_LIBTOOL
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
AC_PROG_CC_C99
AS_IF([test x"$ac_cv_prog_cc_c99" = x"no"],
	AC_MSG_ERROR([C compiler does not support C99], 1))
AC_CHECK_PROG([HAVE_POD2MAN], [pod2man], [yes])
AS_IF([test "$HAVE_POD2MAN" != "yes"],
	AC_MSG_FAILURE([pod2man is needed to generate manual from POD]))


AC_ARG_ENABLE(
		asm,
		[AC_HELP_STRING([--enable-asm],[Enable native Assembly code])],
		ASM=$enableval,
		ASM=yes
             )

if test x"$ASM" = x"yes"; then
	AC_CHECK_PROGS( ASM_PROG, nasm yasm, no,  ) # Allow test for multiple Assemblers.
	if test x"$ASM_PROG" = x"no"; then
		ASM=no
	fi
## only for x86 and x86_64
	case $host in
		i?86-*)
			ASM_OPT="-g -F dwarf -f elf"
			USE_64=no
			;;
		x86_64-*)
			ASM_OPT="-Dx64 -g -F dwarf -f elf64"
			USE_64=yes
			;;
		*) 
			ASM_PROG=
			ASM_OPT=
			ASM=no
			;;
	esac
fi

static=no
AC_ARG_ENABLE([static-bin],
	[AC_HELP_STRING([--enable-static-bin],[Build statically linked binary @<:@default=no@:>@])],
	[static=$enableval]
	)
AM_CONDITIONAL([STATIC], [test x"$static" = x"yes"])

AC_CHECK_HEADERS(fcntl.h sys/time.h unistd.h sys/mman.h)
AC_CHECK_HEADERS(ctype.h errno.h sys/resource.h)
AC_CHECK_HEADERS(endian.h sys/endian.h arpa/inet.h)
AC_CHECK_HEADERS(alloca.h pthread.h)
AC_CHECK_HEADERS(gcrypt.h, gpg-error.h)

AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_C___ATTRIBUTE__
AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(long)
AC_CHECK_SIZEOF(short)

AC_CACHE_CHECK([for large file support],rzip_cv_HAVE_LARGE_FILES,[
AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <stdio.h>
#include <sys/types.h>
main() { return (sizeof(off_t) == 4); }]])],[rzip_cv_HAVE_LARGE_FILES=yes],[rzip_cv_HAVE_LARGE_FILES=no],[rzip_cv_HAVE_LARGE_FILES=cross])])
if test x"$rzip_cv_HAVE_LARGE_FILES" = x"yes"; then
	AC_DEFINE(HAVE_LARGE_FILES, 1, [ ])
fi

AC_C_INLINE

AC_C_BIGENDIAN

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
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
CXXFLAGS="$CXXFLAGS $PTHREAD_CXXFLAGS"

## test for ARM or other non x86 processor
## must use -DNOJIT for compiling zpaq
case $host in
	*86*)
		;;
	*)
		CXXFLAGS+=" -DNOJIT"
		;;
esac

AM_CONDITIONAL([USE_ASM], [test x"$ASM" = x"yes"])
AM_CONDITIONAL([USE_X64], [test x"$USE_64" = x"yes"])
AC_SUBST([ASM_OPT])
EFL_CHECK_DOXYGEN([build_doc="yes"], [build_doc="no"])

AC_CONFIG_FILES([
Makefile
src/Makefile
src/lzma/Makefile
src/lzma/C/Makefile
src/lzma/ASM/Makefile
src/lib/libzpaq/Makefile
doc/Makefile
man/Makefile
])
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
echo
echo "Documentation..........: ${build_doc}"
echo
echo "Compilation............: make (or gmake)"
echo "  CPPFLAGS.............: $CPPFLAGS"
echo "  CFLAGS...............: $CFLAGS"
echo "  CXXFLAGS.............: $CXXFLAGS"
echo -n "  ASM..................: "
if test x"$ASM_PROG" = x"no"; then
	echo "$ASM"
else
	echo "$ASM, using $ASM_PROG Assembler"
	echo "  ASMFLAGS.............: $ASM_OPT"
fi
echo "  LDFLAGS..............: $LDFLAGS"
echo
echo "Installation...........: make install (as root if needed, with 'su' or 'sudo')"
echo "  prefix...............: $prefix"
if test x"$static" = x"yes"; then
gver=$(ldd --version | head -n1 | rev | cut -f1 -d\ | rev)
	echo "WARNING: Static Binary will require runtime glibc libraries of the same version used to link it here: $gver. Therefore static binary may not be portable to other systems."
fi
echo
