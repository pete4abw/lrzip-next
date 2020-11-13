MAINTAINERCLEANFILES = Makefile.in

AM_CFLAGS = -I . -I @top_srcdir@ -I @top_srcdir@/src/include -I @top_srcdir@/src/lzma/include -DNDEBUG
AM_CXXFLAGS = $(AM_CFLAGS)

noinst_LTLIBRARIES = libtmplrzip.la
libtmplrzip_la_SOURCES = \
	lrzip_private.h \
	liblrzip_private.h \
	lrzip.c \
	lrzip_core.h \
	rzip.h \
	rzip.c \
	runzip.c \
	runzip.h \
	stream.c \
	stream.h \
	util.c \
	util.h \
	lzma/include/7zCrc.h \
	md5.c \
	md5.h \
	aes.c \
	aes.h \
	sha4.c \
	sha4.h

libtmplrzip_la_LIBADD = lzma/C/liblzma.la	\
	lib/libzpaq/libzpaq.la	\
	lib/minilzo/libminilzo.la

if USE_ASM
libtmplrzip_la_LIBADD += lzma/ASM/liblzmaASM.la
endif

lib_LTLIBRARIES = liblrzip.la
liblrzip_la_SOURCES = \
	liblrzip.c \
	liblrzip_private.h
nodist_EXTRA_liblrzip_la_SOURCES = dummy.cxx
liblrzip_la_LIBADD = libtmplrzip.la

lrztardir = $(bindir)
lrztar_SCRIPTS = lrztar

bin_PROGRAMS = lrzip
lrzip_SOURCES = \
	main.c
nodist_EXTRA_lrzip_SOURCES = dummyy.cxx

lrzip_LDADD = libtmplrzip.la
if STATIC
	lrzip_LDFLAGS = -all-static
endif

lrzip_HEADERS = include/Lrzip.h
lrzipdir = $(includedir)

noinst_PROGRAMS = decompress_demo liblrzip_demo
decompress_demo_SOURCES = decompress_demo.c
decompress_demo_LDADD = liblrzip.la

liblrzip_demo_SOURCES = liblrzip_demo.c
liblrzip_demo_LDADD = liblrzip.la
