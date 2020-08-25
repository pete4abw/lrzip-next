MAINTAINERCLEANFILES = Makefile.in
SUBDIRS = C

if USE_ASM
SUBDIRS += ASM
endif

lzmadocdir = @docdir@/lzma
lzmadoc_DATA = \
  lzma-history.txt \
  lzma-sdk.txt \
  lzma-specification.txt \
  lzma.txt \
  Methods.txt \
  README \
  README-Alloc \
  README.threading

if USE_X64
lzmadoc_DATA += README.ASMDecompress
endif

EXTRA_DIST = $(lzmadoc_DATA)
