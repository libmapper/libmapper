
lib_LTLIBRARIES = libmapperjni.la
libmapperjni_la_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS) @JNIFLAGS@
libmapperjni_la_SOURCES = mapperjni.c
libmapperjni_la_LDFLAGS = -export-dynamic -version-info @SO_VERSION@
libmapperjni_la_LIBADD = \
    $(top_builddir)/src/libmapper.la $(liblo_LIBS)

JHEADERS = mapper_Database.h                                                \
           mapper_device_Query.h mapper_Device.h                            \
           mapper_link_Query.h mapper_Link.h                                \
           mapper_map_Query.h mapper_Map.h mapper_Map_Slot.h                \
           mapper_Network.h                                                 \
           mapper_signal_Query.h mapper_Signal.h mapper_Signal_Instance.h   \
           mapper_TimeTag.h

JFLAGS = -source 1.7 -target 1.7
JCLASSES = mapper/NativeLib.class mapper/Database.class                     \
           mapper/database/DeviceListener.class mapper/database/Event.class \
           mapper/database/LinkListener.class                               \
           mapper/database/MapListener.class mapper/database/Operator.class \
           mapper/database/SignalListener.class                             \
           mapper/database/ObjectType.class                                 \
           mapper/Device.class mapper/device/LinkListener.class             \
           mapper/device/MapListener.class mapper/device/Query.class        \
           mapper/Direction.class                                           \
           mapper/Link.class mapper/link/Query.class                        \
           mapper/Map.class mapper/map/BoundaryAction.class                 \
           mapper/map/Location.class mapper/map/Mode.class                  \
           mapper/map/Query.class                                           \
           mapper/Network.class                                             \
           mapper/Property.class                                            \
           mapper/Signal.class                                              \
           mapper/signal/InstanceEvent.class                                \
           mapper/signal/InstanceEventListener.class                        \
           mapper/signal/InstanceUpdateListener.class                       \
           mapper/signal/Query.class mapper/signal/StealingMode.class       \
           mapper/signal/UpdateListener.class                               \
           mapper/TimeTag.class                                             \
           mapper/Value.class

JCLASSESINTERNAL = mapper/device/Query$$1.class mapper/link/Query$$1.class  \
                   mapper/map/Query$$1.class mapper/Map$$Slot.class         \
                   mapper/Value$$Exception.class                            \
                   mapper/Signal$$Instance.class                            \
                   mapper/signal/Query$$1.class

JJAVA = $(JCLASSES:%.class=%.java)

BUILT_SOURCES = $(JHEADERS) test.class testquery.class testreverse.class \
    testspeed.class libmapper.jar
MOSTLYCLEANFILES = $(BUILT_SOURCES) $(JCLASSES) $(subst $$,\$$,$(JCLASSESINTERNAL)) \
    $(subst $$,\$$,test$$1.class test$$2.class test$$3.class testquery$$1.class \
    testquery$$2.class testquery$$3.class testreverse$$1.class testreverse$$2.class \
    testreverse$$3.class testspeed$$1.class testspeed$$2.class testspeed$$3.class)

mapper_%.h: mapper/%.java
	$(JAVAC) -h . $(<:%.java=%).java
	if [ -f $@ ]; then touch $@; fi

mapper_database_%.h: mapper/database/%.java
	$(JAVAC) -h . $(<:%.java=%)
	if [ -f $@ ]; then touch $@; fi

mapper_device_%.h: mapper/device/%.java
	$(JAVAC) -h . $(<:%.java=%)
	if [ -f $@ ]; then touch $@; fi

mapper_link_%.h: mapper/link/%.java
	$(JAVAC) -h . $(<:%.java=%)
	if [ -f $@ ]; then touch $@; fi

mapper_map_%.h: mapper/map/%.java
	$(JAVAC) -h . $(<:%.java=%)
	if [ -f $@ ]; then touch $@; fi

mapper_signal_%.h: mapper/signal/%.java
	$(JAVAC) -h . $(<:%.java=%)
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

libmapper.jar: $(JCLASSES) $(JCLASSESINTERNAL)
	$(JAR) cvf $@ $(subst $$,\$$,$^)
	if mkdir -v TestInstances/code; then \
	  cd TestInstances/code/; \
	  ln -v -s ../../.libs/*.so .; \
	  ln -v -s ../../$@; \
	  fi # For processing test sketch

EXTRA_DIST = $(JJAVA) test.java testquery.java testreverse.java testspeed.java \
             $(JHEADERS)
