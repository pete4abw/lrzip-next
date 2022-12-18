MAINTAINERCLEANFILES = Makefile.in

AM_CFLAGS = -I . -I @top_srcdir@ -I @top_srcdir@/src/include -I @top_srcdir@/src/lzma/include
AM_CXXFLAGS = $(AM_CFLAGS)

noinst_LTLIBRARIES = libtmplrzip_next.la
libtmplrzip_next_la_SOURCES = \
	lrzip.c \
	rzip.c \
	runzip.c \
	stream.c \
	util.c \
	include/lrzip_core.h \
	include/lrzip_private.h \
	include/rzip.h \
	include/runzip.h \
	include/stream.h \
	include/util.h \
	lzma/include/LzmaDec.h \
	lzma/include/LzmaLib.h


libtmplrzip_next_la_LIBADD =  \
	lzma/C/liblzma.la	\
	lib/libzpaq/libzpaq.la \
	lib/libbz3/libbz3.la

if USE_ASM
libtmplrzip_next_la_LIBADD += lzma/ASM/liblzmaASM.la
endif

lrzntardir = $(bindir)
lrzntar_SCRIPTS = lrzntar

bin_PROGRAMS = lrzip-next
lrzip_next_SOURCES = \
	main.c
nodist_EXTRA_lrzip_next_SOURCES = dummyy.cxx

lrzip_next_LDADD = libtmplrzip_next.la
if STATIC
lrzip_next_LDFLAGS = -all-static
endif
