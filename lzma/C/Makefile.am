MAINTAINERCLEANFILES = Makefile.in

## for readability
## debugger requires absolute paths to find sources in ASM
LZMA_BASEDIR = @abs_top_srcdir@/lzma

AM_CFLAGS = \
  -D_REENTRANT \
  -D_7ZIP_LARGE_PAGES \
  -I@top_builddir@ \
  -I@top_srcdir@ \
  -I$(LZMA_BASEDIR)/include

C_S =
if USE_ASM
  AM_CFLAGS += -D_7ZIP_ASM
if USE_X64
## For LZMA Assembler Decompressor
  AM_CFLAGS += -D_LZMA_DEC_OPT
endif
else
  C_S += 7zCrcOpt.c
endif


noinst_LTLIBRARIES = liblzma.la
liblzma_la_SOURCES = \
	7zCrc.c 	$(LZMA_BASEDIR)/include/7zCrc.h \
	$(LZMA_BASEDIR)/include/7zTypes.h $(C_S) \
	Alloc.c 	$(LZMA_BASEDIR)/include/Alloc.h \
	Bra86.c		$(LZMA_BASEDIR)/include/Bra.h \
	Bra.c \
	BraIA64.c \
	$(LZMA_BASEDIR)/include/Compiler.h \
	$(LZMA_BASEDIR)/include/CpuArch.h \
	Delta.c		$(LZMA_BASEDIR)/include/Delta.h \
	LzFind.c 	$(LZMA_BASEDIR)/include/LzFind.h \
	LzFindMt.c 	$(LZMA_BASEDIR)/include/LzFindMt.h \
	$(LZMA_BASEDIR)/include/LzHash.h \
	LzmaDec.c 	$(LZMA_BASEDIR)/include/LzmaDec.h \
	LzmaEnc.c 	$(LZMA_BASEDIR)/include/LzmaEnc.h \
	LzmaLib.c 	$(LZMA_BASEDIR)/include/LzmaLib.h \
	Threads.c 	$(LZMA_BASEDIR)/include/Threads.h \
	$(LZMA_BASEDIR)/include/precomp.h \
	$(LZMA_BASEDIR)/include/basetyps.h \
	$(LZMA_BASEDIR)/include/MyGuidDef.h \
	$(LZMA_BASEDIR)/include/MyWindowsDef.h \
	$(LZMA_BASEDIR)/include/windows.h
