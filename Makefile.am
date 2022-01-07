SUBDIRS = src include test examples bindings @JNI@ @DOXYGEN@ extra

EXTRA_DIST = libtool ltmain.sh autogen.sh libmapper.pc.in

clean-local:
	-rm -rf libmapper.pc

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libmapper.pc

dist_doc_DATA = README COPYING ChangeLog NEWS

ACLOCAL_AMFLAGS = -I m4
