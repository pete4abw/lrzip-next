MAINTAINERCLEANFILES = Makefile.in

## zpaq recommends best optimization for speed
## define unix for Apple and Linux
CXXFLAGS += -O3 -Dunix

noinst_LTLIBRARIES = libzpaq.la
libzpaq_la_SOURCES = \
	libzpaq.cpp \
	libzpaq.h

zpaqdocdir = @docdir@/zpaq
zpaqdoc_DATA = \
       doc/COPYING \
       doc/readme.txt \
       doc/zpaq206.pdf
