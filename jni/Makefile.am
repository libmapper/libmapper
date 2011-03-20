
lib_LTLIBRARIES = libmapperjni-@MAJOR_VERSION@.la
libmapperjni_@MAJOR_VERSION@_la_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS) -I@JNIPATH@
libmapperjni_@MAJOR_VERSION@_la_SOURCES = mapperjni.c
libmapperjni_@MAJOR_VERSION@_la_LDFLAGS = -export-dynamic -version-info @SO_VERSION@
libmapperjni_@MAJOR_VERSION@_la_LIBADD = \
	$(top_builddir)/src/libmapper-@MAJOR_VERSION@.la $(liblo_LIBS)

JHEADERS = Mapper_Device.h Mapper_Device_Signal.h Mapper_Db_Signal.h

JCLASSES = Mapper/Db/Signal.class Mapper/Device$$Signal.class		\
           Mapper/Device$$1.class									\
           Mapper/PropertyValue$$PropertyException.class			\
           Mapper/InputListener.class Mapper/PropertyValue.class	\
           Mapper/Device.class Mapper/NativeLib.class

BUILT_SOURCES = $(JHEADERS) test.class libmapper-@MAJOR_VERSION@.jar
CLEANFILES = $(JHEADERS) $(subst $$,\$$,$(wildcard *.class Mapper/*.class Mapper/*/*.class))

Mapper_%.h: Mapper/%.class
	$(JAVAH) $(subst /,.,$(<:%.class=%))
	if [ -f $@ ]; then touch $@; fi

Mapper_Db_%.h: Mapper/Db/%.class
	$(JAVAH) $(subst /,.,$(<:%.class=%))
	if [ -f $@ ]; then touch $@; fi

Mapper/%.class: Mapper/%.java
	$(JAVAC) $<

test.class: test.java $(JCLASSES)
	$(JAVAC) $<

libmapper-@MAJOR_VERSION@.jar: $(JCLASSES)
	$(JAR) cvf $@ $(subst $$,\$$,$^)

EXTRA_DIST = Mapper/Device.java Mapper/Property.java
