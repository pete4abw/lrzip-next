ACLOCAL_AMFLAGS = -I m4
MAINTAINERCLEANFILES = \
	Makefile.in \
	aclocal.m4 \
	config.guess \
	config.h.in \
	config.sub \
	configure \
	depcomp \
	install-sh \
	ltconfig \
	ltmain.sh \
	missing \
	$(PACKAGE_TARNAME)-$(PACKAGE_VERSION).tar.gz \
	$(PACKAGE_TARNAME)-$(PACKAGE_VERSION).tar.bz2 \
	$(PACKAGE_TARNAME)-$(PACKAGE_VERSION)-doc.tar.bz2 \
	m4/libtool.m4 \
	m4/lt~obsolete.m4 \
	m4/ltoptions.m4 \
	m4/ltsugar.m4 \
	m4/ltversion.m4

SUBDIRS = src/lzma \
	  src/lib/libzpaq \
	  src/lib/libbz3 \
	  src \
	  man \
	  doc

dist_doc_DATA = \
	AUTHORS \
	BUGS \
	ChangeLog \
	COPYING \
	README.md \
	README-NOT-BACKWARD-COMPATIBLE \
	TODO \
	WHATS-NEW

EXTRA_DIST = \
	src/lrzntar \
	util/gitdesc.sh \
	description-pak \
	autogen.sh \
	INSTALL \
	VERSION \
	$(dist_doc_DATA)

install-exec-hook:
	$(LN_S) -f lrzip-next$(EXEEXT) $(DESTDIR)$(bindir)/lrznunzip$(EXEEXT)
	$(LN_S) -f lrzip-next$(EXEEXT) $(DESTDIR)$(bindir)/lrzncat$(EXEEXT)
	$(LN_S) -f lrzntar$(EXEEXT) $(DESTDIR)$(bindir)/lrznuntar$(EXEEXT)


uninstall-local:
	rm -f $(bindir)/lrznunzip
	rm -f $(bindir)/lrzncat
	rm -f $(bindir)/lrznuntar

.PHONY: doc

# Documentation

doc: all
	@echo "entering doc/"
	$(MAKE) -C doc doc

