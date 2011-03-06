SUBDIRS = src include test examples @SWIG@ @JNI@ @DOXYGEN@ extra

EXTRA_DIST = libtool ltmain.sh autogen.sh libmapper.pc.in

libmapper-@MAJOR_VERSION@.pc: libmapper.pc
	@if ! [ -e $@ ]; then cp -v $< $@; fi

clean-local:
	-rm -rf libmapper-@MAJOR_VERSION@.pc

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libmapper-@MAJOR_VERSION@.pc

dist_doc_DATA = README COPYING ChangeLog NEWS

ACLOCAL_AMFLAGS = -I m4
