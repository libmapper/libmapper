
lib_LTLIBRARIES = libmapperjni.la
libmapperjni_la_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS) @JNIFLAGS@
libmapperjni_la_SOURCES = mapperjni.c
libmapperjni_la_LDFLAGS = -export-dynamic -version-info @SO_VERSION@
libmapperjni_la_LIBADD = \
    $(top_builddir)/src/libmapper.la $(liblo_LIBS)

JHEADERS = mapper_AbstractObject.h                                          \
           mapper_Graph.h                                                   \
           mapper_Device.h                                                  \
           mapper_List.h                                                    \
           mapper_Map.h                                                     \
           mapper_Signal.h mapper_Signal_Instance.h                         \
           mapper_Time.h

JFLAGS = -source 1.8 -target 1.8
JCLASSES = mapper/NativeLib.class mapper/Graph.class                        \
           mapper/graph/DeviceListener.class mapper/graph/Event.class       \
           mapper/graph/MapListener.class mapper/graph/Operator.class       \
           mapper/graph/SignalListener.class                                \
           mapper/graph/ObjectType.class                                    \
           mapper/Device.class                                              \
           mapper/Direction.class                                           \
           mapper/List.class                                                \
           mapper/Map.class                                                 \
           mapper/map/Location.class                                        \
           mapper/Property.class                                            \
           mapper/Signal.class                                              \
           mapper/signal/InstanceEvent.class                                \
           mapper/signal/InstanceEventListener.class                        \
           mapper/signal/InstanceUpdateListener.class                       \
           mapper/signal/StealingMode.class                                 \
           mapper/signal/UpdateListener.class                               \
           mapper/Time.class                                                \
           mapper/Type.class

JCLASSESINTERNAL = mapper/Signal$$Instance.class

JJAVA = $(JCLASSES:%.class=%.java)

BUILT_SOURCES = $(JHEADERS) test.class testreverse.class testspeed.class    \
    libmapper.jar
MOSTLYCLEANFILES = $(BUILT_SOURCES) $(JCLASSES) $(subst $$,\$$,$(JCLASSESINTERNAL)) \
    $(subst $$,\$$,test$$1.class test$$2.class test$$3.class                \
    testreverse$$1.class testreverse$$2.class testreverse$$3.class          \
    testspeed$$1.class testspeed$$2.class testspeed$$3.class)

mapper_%.h: mapper/%.class
	$(JAVAH) $(subst /,.,$(<:%.class=%))
	if [ -f $@ ]; then touch $@; fi

mapper_device_%.h: mapper/device/%.class
	$(JAVAH) $(subst /,.,$(<:%.class=%))
	if [ -f $@ ]; then touch $@; fi

mapper_graph_%.h: mapper/graph/%.class
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

EXTRA_DIST = $(JJAVA) test.java testreverse.java testspeed.java \
             $(JHEADERS)
