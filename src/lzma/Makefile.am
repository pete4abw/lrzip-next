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
	doc/README.threading \
	doc/README.ASMDecompress

EXTRA_DIST = $(lzmadoc_DATA)
