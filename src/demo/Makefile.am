MAINTAINERCLEANFILES = Makefile.in

noinst_PROGRAMS = decompress_demo liblrzip_demo
decompress_demo_SOURCES = decompress_demo.c
decompress_demo_LDADD = @top_srcdir@/src/liblrzip_next.la

liblrzip_demo_SOURCES = liblrzip_demo.c
liblrzip_demo_LDADD = @top_srcdir@/src/liblrzip_next.la

demodatadir = $(datarootdir)/$(PACKAGE)
demodata_DATA = decompress_demo.c \
		liblrzip_demo.c
