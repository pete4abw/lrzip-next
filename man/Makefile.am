MAINTAINERCLEANFILES = Makefile.in lrznunzip.1 lrzntar.1 lrznuntar.1 lrzn.1

man1_MANS = lrzip-next.1 lrznunzip.1 lrzncat.1 lrzntar.1 lrznuntar.1 lrzn.1
man5_MANS = lrzip-next.conf.5

BUILT_SOURCES = lrznunzip.1 lrzncat.1 lrzntar.1 lrznuntar.1 lrzn.1
CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = lrzip-next.1 lrznunzip.1.pod lrzncat.1.pod lrzntar.1.pod lrznuntar.1.pod lrzn.1.pod $(man5_MANS)

SUFFIXES = .1 .1.pod
.1.pod.1:
	pod2man $< $@
