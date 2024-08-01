SUBDIRS = src include test examples bindings @DOXYGEN@ extra

EXTRA_DIST = libtool ltmain.sh autogen.sh libmapper.pc.in CMakeLists.txt windows_build.ps1 \
             icons/libmapper_logo_black_512px.png icons/libmapper_logo_black_800px.png \
             icons/libmapper_logo_black_flat.pdf icons/libmapper_logo_flat_black.png \
             icons/libmapper.icns

clean-local:
	-rm -rf libmapper.pc

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libmapper.pc

dist_doc_DATA = README COPYING ChangeLog NEWS

ACLOCAL_AMFLAGS = -I m4
