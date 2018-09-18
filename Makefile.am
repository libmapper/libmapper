SUBDIRS = src include test examples @SWIGDIR@ @JNI@ @DOXYGEN@ extra

EXTRA_DIST = libtool ltmain.sh autogen.sh libmpr.pc.in

clean-local:
	-rm -rf libmpr.pc

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libmpr.pc

dist_doc_DATA = README COPYING ChangeLog NEWS

ACLOCAL_AMFLAGS = -I m4
