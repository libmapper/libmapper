
lib_LTLIBRARIES = libmapperjni-@MAJOR_VERSION@.la
libmapperjni_@MAJOR_VERSION@_la_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS) -I@JNIPATH@
libmapperjni_@MAJOR_VERSION@_la_SOURCES = mapperjni.c Mapper.h
libmapperjni_@MAJOR_VERSION@_la_LDFLAGS = -export-dynamic -version-info @SO_VERSION@

Mapper.h: Mapper.class
	javah $(<:%.class=%)

Mapper.class: Mapper.java
	javac $<

test.class: test.java
	javac $<

EXTRA_DIST = Mapper.java
