
lib_LTLIBRARIES = libmapperjni-@MAJOR_VERSION@.la
libmapperjni_@MAJOR_VERSION@_la_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS) -I@JNIPATH@
libmapperjni_@MAJOR_VERSION@_la_SOURCES = mapperjni.c
libmapperjni_@MAJOR_VERSION@_la_LDFLAGS = -export-dynamic -version-info @SO_VERSION@
libmapperjni_@MAJOR_VERSION@_la_LIBADD = \
    $(top_builddir)/src/libmapper-@MAJOR_VERSION@.la $(liblo_LIBS)

JHEADERS = mapper_Db.h mapper_device_Query.h mapper_Device.h                \
           mapper_map_Query.h mapper_Map.h mapper_Map_Slot.h                \
           mapper_Network.h mapper_signal_Query.h mapper_Signal.h           \
           mapper_Signal_Instance.h

JFLAGS = -source 1.5 -target 1.5
JCLASSES = mapper/NativeLib.class mapper/Db.class                           \
           mapper/db/DeviceListener.class mapper/db/Event.class             \
           mapper/db/MapListener.class mapper/db/Operator.class             \
           mapper/db/SignalListener.class mapper/db/SubscriptionType.class  \
           mapper/Device.class mapper/device/Query.class                    \
           mapper/Direction.class                                           \
           mapper/Map.class mapper/map/BoundaryAction.class                 \
           mapper/map/Location.class mapper/map/Mode.class                  \
           mapper/map/Query.class                                           \
           mapper/Network.class                                             \
           mapper/Value.class                                               \
           mapper/Signal.class                                              \
           mapper/signal/InstanceEvent.class                                \
           mapper/signal/InstanceEventListener.class                        \
           mapper/signal/InstanceUpdateListener.class                       \
           mapper/signal/Query.class mapper/signal/StealingMode.class       \
           mapper/signal/UpdateListener.class                               \
           mapper/Timetag.class

JCLASSESINTERNAL = mapper/device/Query$$1.class mapper/map/Query$$1.class   \
                   mapper/Map$$Slot.class                                   \
                   mapper/Value$$Exception.class                            \
                   mapper/Signal$$Instance.class                            \
                   mapper/signal/Query$$1.class

JJAVA = $(JCLASSES:%.class=%.java)

BUILT_SOURCES = $(JHEADERS) test.class testquery.class testreverse.class \
    testspeed.class libmapper-@MAJOR_VERSION@.jar
MOSTLYCLEANFILES = $(BUILT_SOURCES) $(JCLASSES) $(subst $$,\$$,$(JCLASSESINTERNAL)) \
    $(subst $$,\$$,test$$1.class test$$2.class test$$3.class testquery$$1.class \
    testquery$$2.class testquery$$3.class testreverse$$1.class testreverse$$2.class \
    testreverse$$3.class testspeed$$1.class testspeed$$2.class testspeed$$3.class)

mapper_%.h: mapper/%.class
	$(JAVAH) $(subst /,.,$(<:%.class=%))
	if [ -f $@ ]; then touch $@; fi

mapper_db_%.h: mapper/db/%.class
	$(JAVAH) $(subst /,.,$(<:%.class=%))
	if [ -f $@ ]; then touch $@; fi

mapper_device_%.h: mapper/device/%.class
	$(JAVAH) $(subst /,.,$(<:%.class=%))
	if [ -f $@ ]; then touch $@; fi

mapper_map_%.h: mapper/map/%.class
	$(JAVAH) $(subst /,.,$(<:%.class=%))
	if [ -f $@ ]; then touch $@; fi

mapper_signal_%.h: mapper/signal/%.class
	$(JAVAH) $(subst /,.,$(<:%.class=%))
	if [ -f $@ ]; then touch $@; fi

mapper/%.class: mapper/%.java
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
