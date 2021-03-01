MAINTAINERCLEANFILES = Makefile.in

AM_CFLAGS = -I . -I @top_srcdir@ -I @top_srcdir@/src/include -I @top_srcdir@/src/lzma/include -DNDEBUG
AM_CXXFLAGS = $(AM_CFLAGS)

noinst_LTLIBRARIES = libtmplrzip_next.la
libtmplrzip_next_la_SOURCES = \
	lrzip.c \
	rzip.c \
	runzip.c \
	stream.c \
	util.c \
	md5.c \
	aes.c \
	sha4.c \
	include/liblrzip_private.h \
	include/lrzip_core.h \
	include/lrzip_private.h \
	include/rzip.h \
	include/runzip.h \
	include/stream.h \
	include/util.h \
	include/aes.h \
	include/md5.h \
	include/sha4.h \
	lzma/include/7zCrc.h \
	lzma/include/LzmaDec.h \
	lzma/include/LzmaLib.h


libtmplrzip_next_la_LIBADD = lzma/C/liblzma.la	\
	lib/libzpaq/libzpaq.la	\
	lib/minilzo/libminilzo.la

if USE_ASM
libtmplrzip_next_la_LIBADD += lzma/ASM/liblzmaASM.la
endif

lib_LTLIBRARIES = liblrzip_next.la
liblrzip_next_la_SOURCES = \
	liblrzip.c \
	include/liblrzip_private.h
nodist_EXTRA_liblrzip_next_la_SOURCES = dummy.cxx
liblrzip_next_la_LIBADD = libtmplrzip_next.la

lrztardir = $(bindir)
lrztar_SCRIPTS = lrztar

bin_PROGRAMS = lrzip-next
lrzip_next_SOURCES = \
	main.c
nodist_EXTRA_lrzip_next_SOURCES = dummyy.cxx

lrzip_next_LDADD = libtmplrzip_next.la
if STATIC
	lrzip_next_LDFLAGS = -all-static
endif

lrzip_next_HEADERS = include/Lrzip.h
lrzip_nextdir = $(includedir)
