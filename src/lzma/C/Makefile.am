MAINTAINERCLEANFILES = Makefile.in

AM_CFLAGS = \
  -D_REENTRANT \
  -D_7ZIP_LARGE_PAGES \
  -I@top_srcdir@/src \
  -I../include

C_S =
if USE_ASM
if USE_X64
## For LZMA Assembler Decompressor
  AM_CFLAGS += -D_LZMA_DEC_OPT -DMFMT_GM_INLINE
endif
endif

noinst_LTLIBRARIES = liblzma.la
liblzma_la_SOURCES = \
	Alloc.c 	../include/Alloc.h \
	Bra86.c		../include/Bra.h \
	Bra.c \
	BraIA64.c \
	../include/Compiler.h \
	CpuArch.c	../include/CpuArch.h \
	Delta.c		../include/Delta.h \
	LzFind.c 	../include/LzFind.h \
	LzFindMt.c 	../include/LzFindMt.h \
	../include/LzHash.h \
	LzmaDec.c 	../include/LzmaDec.h \
	LzmaEnc.c 	../include/LzmaEnc.h \
	LzmaLib.c 	../include/LzmaLib.h \
	Threads.c 	../include/Threads.h \
	../include/Precomp.h
