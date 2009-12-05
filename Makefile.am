SUBDIRS = src include # @DOXYGEN@

EXTRA_DIST = libtool ltmain.sh autogen.sh libmapper.pc.in

libmapper-@MAJOR_VERSION@.pc: libmapper.pc
	@if ! [ -e $@ ]; then ln -v $< $@; fi

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libmapper-@MAJOR_VERSION@.pc

dist_doc_DATA = README COPYING ChangeLog NEWS
