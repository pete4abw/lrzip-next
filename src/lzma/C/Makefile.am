MAINTAINERCLEANFILES = Makefile.in

AM_CFLAGS = \
  -D_REENTRANT \
  -D_7ZIP_LARGE_PAGES \
  -I@top_srcdir@/src \
  -I../include

C_S =
if USE_ASM
## For LZMA Assembler Decompressor and LzFind Match Finder
  AM_CFLAGS += -D_LZMA_DEC_OPT
else
## Add LzFindOpt.c to compile list if not using Assembler
  C_S += LzFindOpt.c
endif

noinst_LTLIBRARIES = liblzma.la
liblzma_la_SOURCES = \
	Alloc.c 	../include/Alloc.h \
	Bra86.c		../include/Bra.h \
	Bra.c \
	BraIA64.c \
	../include/Compiler.h \
	../include/7zTypes.h \
	CpuArch.c	../include/CpuArch.h \
	Delta.c		../include/Delta.h \
	LzFind.c 	../include/LzFind.h \
	LzFindMt.c 	../include/LzFindMt.h \
	../include/LzHash.h \
	LzmaDec.c 	../include/LzmaDec.h \
	LzmaEnc.c 	../include/LzmaEnc.h \
	LzmaLib.c 	../include/LzmaLib.h \
	Threads.c 	../include/Threads.h \
	../include/Precomp.h \
	$(C_S)
