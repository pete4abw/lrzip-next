MAINTAINERCLEANFILES = Makefile.in
SUBDIRS = C

if USE_ASM
SUBDIRS += ASM
endif

lzmadocdir = @docdir@/lzma
lzmadoc_DATA = \
	doc/lzma-history.txt \
	doc/lzma-sdk.txt \
	doc/lzma-specification.txt \
	doc/lzma.txt \
	doc/Methods.txt \
	doc/README \
	doc/README-Alloc \
	doc/README.threading

if USE_X64
lzmadoc_DATA += doc/README.ASMDecompress
endif

EXTRA_DIST = $(lzmadoc_DATA)
