if TESTS
TEST_CFLAGS = -Wall -I$(top_srcdir)/include @liblo_CFLAGS@
TEST_CXXFLAGS = -Wall -I$(top_srcdir)/include @liblo_CFLAGS@

if WINDOWS_DLL
TEST_LDADD = $(top_builddir)/src/*.lo $(liblo_LIBS)
noinst_PROGRAMS = test testbundle testcalibrate testconvergent testcpp         \
                  testcustomtransport testexpression testgraph testinstance    \
                  testlinear testlocalmap testmany testmapfail testmapinput    \
                  testmapprotocol testmonitor testnetwork testparams testparser\
                  testprops testrate testreverse testsignalhierarchy           \
                  testsignals testspeed testunmap testvector

test_all_ordered = testparams testprops testgraph testparser testnetwork       \
                   testmany test testlinear testexpression testrate testbundle \
                   testinstance testreverse testvector testcustomtransport     \
                   testspeed testcpp testmapinput testconvergent testunmap     \
                   testmapfail testmapprotocol testcalibrate testlocalmap      \
                   testsignalhierarchy
else
TEST_LDADD = $(top_builddir)/src/libmapper.la $(liblo_LIBS)
noinst_PROGRAMS = test testbundle testcalibrate testconvergent testcpp         \
                  testcustomtransport testexpression testgraph testinstance    \
                  testinterrupt testlinear testlocalmap testmany testmapfail   \
                  testmapinput testmapprotocol testmonitor testnetwork         \
                  testparams testparser testprops testrate testreverse         \
                  testsignalhierarchy testsignals testspeed testthread         \
                  testunmap testvector

test_all_ordered = testparams testprops testgraph testparser testnetwork       \
                   testmany test testlinear testexpression testrate testbundle \
                   testinstance testreverse testvector testcustomtransport     \
                   testspeed testcpp testmapinput testconvergent testunmap     \
                   testmapfail testmapprotocol testcalibrate testlocalmap      \
                   testthread testinterrupt testsignalhierarchy
endif

test_CFLAGS = $(TEST_CFLAGS)
test_SOURCES = test.c
test_LDADD = $(TEST_LDADD)

testbundle_CFLAGS = $(TEST_CFLAGS)
testbundle_SOURCES = testbundle.c
testbundle_LDADD = $(TEST_LDADD)

testcalibrate_CFLAGS = $(TEST_CFLAGS)
testcalibrate_SOURCES = testcalibrate.c
testcalibrate_LDADD = $(TEST_LDADD)

testconvergent_CFLAGS = $(TEST_CFLAGS)
testconvergent_SOURCES = testconvergent.c
testconvergent_LDADD = $(TEST_LDADD)

testcpp_CXXFLAGS = $(TEST_CXXFLAGS)
testcpp_SOURCES = testcpp.cpp
testcpp_LDADD = $(TEST_LDADD)

testcustomtransport_CFLAGS = $(TEST_CFLAGS)
testcustomtransport_SOURCES = testcustomtransport.c
testcustomtransport_LDADD = $(TEST_LDADD)

testexpression_CFLAGS = $(TEST_CFLAGS)
testexpression_SOURCES = testexpression.c
testexpression_LDADD = $(TEST_LDADD)

testgraph_CFLAGS = $(TEST_CFLAGS)
testgraph_SOURCES = testgraph.c
testgraph_LDADD = $(TEST_LDADD)

testinstance_CFLAGS = $(TEST_CFLAGS)
testinstance_SOURCES = testinstance.c
testinstance_LDADD = $(TEST_LDADD)

testinterrupt_CFLAGS = $(TEST_CFLAGS)
testinterrupt_SOURCES = testinterrupt.c
testinterrupt_LDADD = $(TEST_LDADD)

testlinear_CFLAGS = $(TEST_CFLAGS)
testlinear_SOURCES = testlinear.c
testlinear_LDADD = $(TEST_LDADD)

testlocalmap_CFLAGS = $(TEST_CFLAGS)
testlocalmap_SOURCES = testlocalmap.c
testlocalmap_LDADD = $(TEST_LDADD)

testmany_CFLAGS = $(TEST_CFLAGS)
testmany_SOURCES = testmany.c
testmany_LDADD = $(TEST_LDADD)

testmapinput_CFLAGS = $(TEST_CFLAGS)
testmapinput_SOURCES = testmapinput.c
testmapinput_LDADD = $(TEST_LDADD)

testmapfail_CFLAGS = $(TEST_CFLAGS)
testmapfail_SOURCES = testmapfail.c
testmapfail_LDADD = $(TEST_LDADD)

testmapprotocol_CFLAGS = $(TEST_CFLAGS)
testmapprotocol_SOURCES = testmapprotocol.c
testmapprotocol_LDADD = $(TEST_LDADD)

testmonitor_CXXFLAGS = $(TEST_CXXFLAGS)
testmonitor_SOURCES = testmonitor.cpp
testmonitor_LDADD = $(TEST_LDADD)

testnetwork_CFLAGS = $(TEST_CFLAGS)
testnetwork_SOURCES = testnetwork.c
testnetwork_LDADD = $(TEST_LDADD)

testparams_CFLAGS = $(TEST_CFLAGS)
testparams_SOURCES = testparams.c
testparams_LDADD = $(TEST_LDADD)

testparser_CFLAGS = $(TEST_CFLAGS)
testparser_SOURCES = testparser.c
testparser_LDADD = $(TEST_LDADD)

testprops_CFLAGS = $(TEST_CFLAGS)
testprops_SOURCES = testprops.c
testprops_LDADD = $(TEST_LDADD)

testrate_CFLAGS = $(TEST_CFLAGS)
testrate_SOURCES = testrate.c
testrate_LDADD = $(TEST_LDADD)

testreverse_CFLAGS = $(TEST_CFLAGS)
testreverse_SOURCES = testreverse.c
testreverse_LDADD = $(TEST_LDADD)

testsignalhierarchy_CFLAGS = $(TEST_CFLAGS)
testsignalhierarchy_SOURCES = testsignalhierarchy.c
testsignalhierarchy_LDADD = $(TEST_LDADD)

testsignals_CFLAGS = $(TEST_CFLAGS)
testsignals_SOURCES = testsignals.c
testsignals_LDADD = $(TEST_LDADD)

testspeed_CFLAGS = $(TEST_CFLAGS)
testspeed_SOURCES = testspeed.c
testspeed_LDADD = $(TEST_LDADD)

testthread_CFLAGS = $(TEST_CFLAGS)
testthread_SOURCES = testthread.c
testthread_LDADD = $(TEST_LDADD)

testunmap_CFLAGS = $(TEST_CFLAGS)
testunmap_SOURCES = testunmap.c
testunmap_LDADD = $(TEST_LDADD)

testvector_CFLAGS = $(TEST_CFLAGS)
testvector_SOURCES = testvector.c
testvector_LDADD = $(TEST_LDADD)

tests: all
	for i in $(test_all_ordered); do echo Running $$i; ./$$i -qtf; done
	echo Running testmonitor and testsignals; ./testmonitor -qtf & ./testsignals -qtf

memtest: all
	for i in $(noinst_PROGRAMS); do echo Running $$i; if ! LD_PRELOAD=/usr/local/lib/libmapper.dylib valgrind --leak-check=full ./.libs/$$i -qt; then exit 1; fi; done

endif
