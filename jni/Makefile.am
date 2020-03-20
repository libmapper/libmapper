
lib_LTLIBRARIES = libmprjni.la
libmprjni_la_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS) @JNIFLAGS@
libmprjni_la_SOURCES = mprjni.c
libmprjni_la_LDFLAGS = -export-dynamic -version-info @SO_VERSION@
libmprjni_la_LIBADD = \
    $(top_builddir)/src/libmpr.la $(liblo_LIBS)

JHEADERS = mpr_AbstractObject.h mpr_AbstractObject_Properties.h          \
           mpr_Graph.h                                                   \
           mpr_Device.h                                                  \
           mpr_List.h                                                    \
           mpr_Map.h                                                     \
           mpr_Signal.h mpr_Signal_Instance.h                            \
           mpr_Time.h

JFLAGS = -source 1.8 -target 1.8
JCLASSES = mpr/NativeLib.class                                           \
           mpr/AbstractObject.class                                      \
           mpr/Device.class                                              \
           mpr/Direction.class                                           \
           mpr/Graph.class mpr/graph/Event.class                         \
           mpr/graph/Listener.class                                      \
           mpr/List.class                                                \
           mpr/Map.class mpr/map/Location.class                          \
           mpr/Operator.class                                            \
           mpr/Property.class                                            \
           mpr/Signal.class mpr/signal/Event.class                       \
           mpr/signal/Listener.class mpr/signal/StealingType.class       \
           mpr/Time.class                                                \
           mpr/Type.class

JCLASSESINTERNAL = mpr/Signal$$Instance.class

JJAVA = $(JCLASSES:%.class=%.java)

BUILT_SOURCES = $(JHEADERS) test.class testreverse.class testspeed.class libmpr.jar
MOSTLYCLEANFILES = $(BUILT_SOURCES) $(JCLASSES) $(subst $$,\$$,$(JCLASSESINTERNAL)) \
    $(subst $$,\$$,test$$1.class test$$2.class test$$3.class                \
    testreverse$$1.class testreverse$$2.class testreverse$$3.class          \
    testspeed$$1.class testspeed$$2.class testspeed$$3.class)

mpr_%.h: mpr/%.java
	$(JAVAC) -h . $(<:%.java=%).java
	if [ -f $@ ]; then touch $@; fi

mpr_device_%.h: mpr/device/%.java
	$(JAVAC) -h . $(<:%.java=%)
	if [ -f $@ ]; then touch $@; fi

mpr_graph_%.h: mpr/graph/%.java
	$(JAVAC) -h . $(<:%.java=%)
	if [ -f $@ ]; then touch $@; fi

mpr_map_%.h: mpr/map/%.java
	$(JAVAC) -h . $(<:%.java=%)
	if [ -f $@ ]; then touch $@; fi

mpr_signal_%.h: mpr/signal/%.java
	$(JAVAC) -h . $(<:%.java=%)
	if [ -f $@ ]; then touch $@; fi

mpr/%.class: mpr/%.java
	$(JAVAC) $(JFLAGS) -sourcepath @top_srcdir@/jni -d . $<

test.class: test.java $(JCLASSES)
	$(JAVAC) $(JFLAGS) -sourcepath @top_srcdir@/jni -d . $<

testreverse.class: testreverse.java $(JCLASSES)
	$(JAVAC) $(JFLAGS) -sourcepath @top_srcdir@/jni -d . $<

testspeed.class: testspeed.java $(JCLASSES)
	$(JAVAC) $(JFLAGS) -sourcepath @top_srcdir@/jni -d . $<

libmpr.jar: $(JCLASSES) $(JCLASSESINTERNAL)
	$(JAR) cvf $@ $(subst $$,\$$,$^)
	if mkdir -v TestInstances/code; then \
	  cd TestInstances/code/; \
	  ln -v -s ../../.libs/*.so .; \
	  ln -v -s ../../$@; \
	  fi # For processing test sketch

EXTRA_DIST = $(JJAVA) test.java testreverse.java testspeed.java \
             $(JHEADERS)

test_all_ordered = test testqueue testreverse testspeed

tests: all
	for i in $(test_all_ordered); do echo Running $$i; if ! java -Djava.library.path=.libs -cp libmpr.jar:. $$i; then exit 1; fi; done
