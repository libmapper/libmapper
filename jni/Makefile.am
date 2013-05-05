
lib_LTLIBRARIES = libmapperjni-@MAJOR_VERSION@.la
libmapperjni_@MAJOR_VERSION@_la_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS) -I@JNIPATH@
libmapperjni_@MAJOR_VERSION@_la_SOURCES = mapperjni.c
libmapperjni_@MAJOR_VERSION@_la_LDFLAGS = -export-dynamic -version-info @SO_VERSION@
libmapperjni_@MAJOR_VERSION@_la_LIBADD = \
	$(top_builddir)/src/libmapper-@MAJOR_VERSION@.la $(liblo_LIBS)

JHEADERS = Mapper_Device.h Mapper_Device_Signal.h Mapper_Db_Signal.h

JFLAGS = -source 1.5 -target 1.5
JCLASSES = Mapper/Db/Signal.class Mapper/InputListener.class	\
           Mapper/PropertyValue.class Mapper/Device.class		\
           Mapper/NativeLib.class Mapper/TimeTag.class			\
           Mapper/InstanceEventListener.class

JCLASSESINTERNAL = Mapper/Device$$Signal.class Mapper/Device$$1.class	\
                   Mapper/PropertyValue$$PropertyException.class

JJAVA = $(JCLASSES:%.class=%.java)

BUILT_SOURCES = $(JHEADERS) test.class libmapper-@MAJOR_VERSION@.jar
MOSTLYCLEANFILES = $(BUILT_SOURCES) $(JCLASSES) $(subst $$,\$$,$(JCLASSESINTERNAL)) \
	$(subst $$,\$$,test$$1.class test$$2.class test$$3.class)

Mapper_%.h: Mapper/%.class
	$(JAVAH) $(subst /,.,$(<:%.class=%))
	if [ -f $@ ]; then touch $@; fi

Mapper_Db_%.h: Mapper/Db/%.class
	$(JAVAH) $(subst /,.,$(<:%.class=%))
	if [ -f $@ ]; then touch $@; fi

Mapper/%.class: Mapper/%.java
	$(JAVAC) $(JFLAGS) -sourcepath @top_srcdir@/jni -d . $<

test.class: test.java $(JCLASSES)
	$(JAVAC) $(JFLAGS) -sourcepath @top_srcdir@/jni -d . $<

libmapper-@MAJOR_VERSION@.jar: $(JCLASSES) $(JCLASSESINTERNAL)
	$(JAR) cvf $@ $(subst $$,\$$,$^)
	if mkdir -v TestInstances/code; then \
	  cd TestInstances/code/; \
	  ln -v -s ../../.libs/*.so .; \
	  ln -v -s ../../$@; \
	fi # For processing test sketch

EXTRA_DIST = $(JJAVA) test.java $(JHEADERS)
