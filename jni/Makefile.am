
lib_LTLIBRARIES = libmapperjni-@MAJOR_VERSION@.la
libmapperjni_@MAJOR_VERSION@_la_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS) -I@JNIPATH@
libmapperjni_@MAJOR_VERSION@_la_SOURCES = mapperjni.c
libmapperjni_@MAJOR_VERSION@_la_LDFLAGS = -export-dynamic -version-info @SO_VERSION@
libmapperjni_@MAJOR_VERSION@_la_LIBADD = \
	$(top_builddir)/src/libmapper-@MAJOR_VERSION@.la $(liblo_LIBS)

JHEADERS = Mapper_Device.h Mapper_Device_Signal.h Mapper_Db_Signal.h        \
           Mapper_Db_Device.h Mapper_Db_Link.h Mapper_Db_Connection.h       \
           Mapper_Monitor.h Mapper_Monitor_Db.h Mapper_Db_DeviceIterator.h  \
           Mapper_Db_SignalIterator.h Mapper_Db_LinkIterator.h              \
           Mapper_Db_ConnectionIterator.h

JFLAGS = -source 1.5 -target 1.5
JCLASSES = Mapper/NativeLib.class Mapper/Device.class                       \
           Mapper/PropertyValue.class Mapper/TimeTag.class                  \
           Mapper/InputListener.class Mapper/InstanceEventListener.class    \
           Mapper/Monitor.class Mapper/Db/Device.class                      \
           Mapper/Db/Signal.class Mapper/Db/Link.class                      \
           Mapper/Db/Connection.class Mapper/Db/DeviceListener.class        \
           Mapper/Db/SignalListener.class Mapper/Db/LinkListener.class      \
           Mapper/Db/ConnectionListener.class                               \
           Mapper/Db/DeviceCollection.class Mapper/Db/DeviceIterator.class  \
           Mapper/Db/SignalCollection.class Mapper/Db/SignalIterator.class  \
           Mapper/Db/LinkCollection.class Mapper/Db/LinkIterator.class      \
           Mapper/Db/ConnectionCollection.class                             \
           Mapper/Db/ConnectionIterator.class

JCLASSESINTERNAL = Mapper/Device$$Signal.class Mapper/Device$$1.class	\
                   Mapper/PropertyValue$$PropertyException.class        \
                   Mapper/Monitor$$Db.class Mapper/Monitor$$1.class

JJAVA = $(JCLASSES:%.class=%.java)

BUILT_SOURCES = $(JHEADERS) test.class testquery.class testreverse.class \
    testspeed.class libmapper-@MAJOR_VERSION@.jar
MOSTLYCLEANFILES = $(BUILT_SOURCES) $(JCLASSES) $(subst $$,\$$,$(JCLASSESINTERNAL)) \
	$(subst $$,\$$,test$$1.class test$$2.class test$$3.class testquery$$1.class \
    testquery$$2.class testquery$$3.class testreverse$$1.class testreverse$$2.class \
    testreverse$$3.class testspeed$$1.class testspeed$$2.class testspeed$$3.class)

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

testquery.class: testquery.java $(JCLASSES)
	$(JAVAC) $(JFLAGS) -sourcepath @top_srcdir@/jni -d . $<

testreverse.class: testreverse.java $(JCLASSES)
	$(JAVAC) $(JFLAGS) -sourcepath @top_srcdir@/jni -d . $<

testspeed.class: testspeed.java $(JCLASSES)
	$(JAVAC) $(JFLAGS) -sourcepath @top_srcdir@/jni -d . $<

libmapper-@MAJOR_VERSION@.jar: $(JCLASSES) $(JCLASSESINTERNAL)
	$(JAR) cvf $@ $(subst $$,\$$,$^)
	if mkdir -v TestInstances/code; then \
	  cd TestInstances/code/; \
	  ln -v -s ../../.libs/*.so .; \
	  ln -v -s ../../$@; \
	fi # For processing test sketch

EXTRA_DIST = $(JJAVA) test.java testquery.java testreverse.java testspeed.java \
             $(JHEADERS)
