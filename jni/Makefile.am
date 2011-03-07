
lib_LTLIBRARIES = libmapperjni-@MAJOR_VERSION@.la
libmapperjni_@MAJOR_VERSION@_la_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS) -I@JNIPATH@
libmapperjni_@MAJOR_VERSION@_la_SOURCES = mapperjni.c Mapper_Device.h
libmapperjni_@MAJOR_VERSION@_la_LDFLAGS = -export-dynamic -version-info @SO_VERSION@
libmapperjni_@MAJOR_VERSION@_la_LIBADD = \
	$(top_builddir)/src/libmapper-@MAJOR_VERSION@.la $(liblo_LIBS)

Mapper_%.h: Mapper/%.class
	javah $(subst /,.,$(<:%.class=%))

Mapper/%.class: Mapper/%.java
	javac $<

test.class: test.java Mapper/Device.class Mapper/InputListener.class
	javac $<

EXTRA_DIST = Mapper/Device.java
