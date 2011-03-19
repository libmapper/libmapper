
lib_LTLIBRARIES = libmapperjni-@MAJOR_VERSION@.la
libmapperjni_@MAJOR_VERSION@_la_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS) -I@JNIPATH@
libmapperjni_@MAJOR_VERSION@_la_SOURCES = mapperjni.c
libmapperjni_@MAJOR_VERSION@_la_LDFLAGS = -export-dynamic -version-info @SO_VERSION@
libmapperjni_@MAJOR_VERSION@_la_LIBADD = \
	$(top_builddir)/src/libmapper-@MAJOR_VERSION@.la $(liblo_LIBS)

BUILT_SOURCES = Mapper_Device.h Mapper_Device_Signal.h
CLEANFILES = Mapper_Device.h Mapper_Device_Signal.h

Mapper_%.h: Mapper/%.class
	javah $(subst /,.,$(<:%.class=%))

Mapper/%.class: Mapper/%.java
	javac $<

test.class: test.java Mapper/Device.class Mapper/PropertyValue.class
	javac $<

EXTRA_DIST = Mapper/Device.java Mapper/Property.java
