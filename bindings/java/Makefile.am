
lib_LTLIBRARIES = libmapperjni.la
libmapperjni_la_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS) @JNIFLAGS@
libmapperjni_la_SOURCES = mapperjni.c
libmapperjni_la_LDFLAGS = -export-dynamic -version-info @SO_VERSION@
libmapperjni_la_LIBADD = \
    $(top_builddir)/src/libmapper.la $(liblo_LIBS)

JHEADERS = mapper_AbstractObject.h mapper_AbstractObject_Properties.h    \
           mapper_AbstractObject_Properties_Entry.h                      \
           mapper_Graph.h                                                \
           mapper_Device.h                                               \
           mapper_List.h                                                 \
           mapper_Map.h                                                  \
           mapper_Signal.h mapper_Signal_Instance.h                      \
           mapper_Time.h

JFLAGS = -source 1.8 -target 1.8
JCLASSES = mapper/NativeLib.class                                        \
           mapper/AbstractObject.class                                   \
           mapper/Device.class                                           \
           mapper/Graph.class mapper/graph/Event.class                   \
           mapper/graph/Listener.class                                   \
           mapper/List.class                                             \
           mapper/Map.class mapper/map/Location.class                    \
           mapper/map/Protocol.class                                     \
           mapper/Operator.class                                         \
           mapper/Property.class                                         \
           mapper/Signal.class mapper/signal/Direction.class             \
           mapper/signal/Event.class mapper/signal/Listener.class        \
           mapper/signal/Stealing.class                                  \
           mapper/Status.class                                           \
           mapper/Time.class                                             \
           mapper/Type.class

JCLASSESINTERNAL = mapper/Signal$$Instance.class mapper/AbstractObject$$Properties.class \
                   mapper/AbstractObject$$Properties$$Entry.class

JJAVA = $(JCLASSES:%.class=%.java)

BUILT_SOURCES = $(JHEADERS) test.class testinstance.class testreverse.class \
    testspeed.class libmapper.jar TestInstances/code/libmapper.jar TestInstances/code/libmapperjni.@SO_EXT@
MOSTLYCLEANFILES = $(BUILT_SOURCES) $(JCLASSES) $(subst $$,\$$,$(JCLASSESINTERNAL)) \
    test*.class

mapper_%.h: mapper/%.java
	$(JAVAC) -h . -cp .:@top_srcdir@/jni -d . $<
	if [ -f $@ ]; then touch $@; fi

mapper_device_%.h: mapper/device/%.java
	$(JAVAC) -h . -cp .:@top_srcdir@/jni -d . $<
	if [ -f $@ ]; then touch $@; fi

mapper_graph_%.h: mapper/graph/%.java
	$(JAVAC) -h . -cp .:@top_srcdir@/jni -d . $<
	if [ -f $@ ]; then touch $@; fi

mapper_map_%.h: mapper/map/%.java
	$(JAVAC) -h . -cp .:@top_srcdir@/jni -d . $<
	if [ -f $@ ]; then touch $@; fi

mapper_signal_%.h: mapper/signal/%.java
	$(JAVAC) -h . -cp .:@top_srcdir@/jni -d . $<
	if [ -f $@ ]; then touch $@; fi

mapper/%.class: mapper/%.java
	$(JAVAC) $(JFLAGS) -sourcepath @top_srcdir@/jni -cp .:@top_srcdir@/jni -d . $<

test.class: test.java $(JCLASSES)
	$(JAVAC) $(JFLAGS) -sourcepath @top_srcdir@/jni -cp .:@top_srcdir@/jni -d . $<

testinstance.class: testinstance.java $(JCLASSES)
	$(JAVAC) $(JFLAGS) -sourcepath @top_srcdir@/jni -cp .:@top_srcdir@/jni -d . $<

testreverse.class: testreverse.java $(JCLASSES)
	$(JAVAC) $(JFLAGS) -sourcepath @top_srcdir@/jni -cp .:@top_srcdir@/jni -d . $<

testspeed.class: testspeed.java $(JCLASSES)
	$(JAVAC) $(JFLAGS) -sourcepath @top_srcdir@/jni -cp .:@top_srcdir@/jni -d . $<

libmapper.jar: $(JCLASSES) $(JCLASSESINTERNAL)
	$(JAR) cvf $@ $(subst $$,\$$,$^)

# For processing test sketch
TestInstances/code/libmapperjni.@SO_EXT@: $(lib_LTLIBRARIES)
	[ -d TestInstances ] || mkdir -v TestInstances
	[ -d TestInstances/code ]	|| mkdir -v TestInstances/code
	cd TestInstances/code; ls -l; [ -L libmapperjni.@SO_EXT@ ] || [ -f libmapperjni.@SO_EXT@ ] || ln -v -s ../../.libs/libmapperjni.@SO_EXT@ .

# For processing test sketch
TestInstances/code/libmapper.jar: libmapper.jar
	[ -d TestInstances ] || mkdir -v TestInstances
	[ -d TestInstances/code ]	|| mkdir -v TestInstances/code
	cd TestInstances/code; ls -l; [ -L libmapperjni.@SO_EXT@ ] || [ -f libmapperjni.@SO_EXT@ ] || ln -v -s ../../libmapper.jar

EXTRA_DIST = $(JJAVA) test.java testinstance.java testreverse.java testspeed.java \
             $(JHEADERS)

test_all_ordered = test testreverse testinstance testspeed

tests: all
	for i in $(test_all_ordered); do echo Running $$i; if ! java -Djava.library.path=.libs -cp libmapper.jar:. $$i; then exit 1; fi; done
