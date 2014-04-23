
TEST_CFLAGS = -Wall -I$(top_srcdir)/include @liblo_CFLAGS@
TEST_CXXFLAGS = -Wall -I$(top_srcdir)/include @liblo_CFLAGS@

if WINDOWS_DLL
TEST_LDADD = $(top_builddir)/src/*.lo $(liblo_LIBS)
else
TEST_LDADD = $(top_builddir)/src/libmapper-@MAJOR_VERSION@.la $(liblo_LIBS)
endif

noinst_PROGRAMS = test testadmin testcustomtransport testdb testexpression \
                  testinstance testlinear testmany testmonitor testparams \
                  testparser testprops testqueue testquery testrate testrecv \
                  testreverse testselect testsend testsignals testspeed testsync \
                  testvector
if HAVE_LAMBDA
noinst_PROGRAMS += testcpp
endif

test_all_ordered = testparams testprops testdb testparser testadmin testmany \
                   testsend testrecv test testlinear testexpression testqueue \
                   testquery testrate testinstance testreverse testselect \
                   testvector testcustomtransport testspeed testsync testcpp

test_CFLAGS = $(TEST_CFLAGS)
test_SOURCES = test.c
test_LDADD = $(TEST_LDADD)

testadmin_CFLAGS = $(TEST_CFLAGS)
testadmin_SOURCES = testadmin.c
testadmin_LDADD = $(TEST_LDADD)

testcpp_CXXFLAGS = $(TEST_CXXFLAGS)
testcpp_SOURCES = testcpp.cpp
testcpp_LDADD = $(TEST_LDADD)

testcustomtransport_CFLAGS = $(TEST_CFLAGS)
testcustomtransport_SOURCES = testcustomtransport.c
testcustomtransport_LDADD = $(TEST_LDADD)

testdb_CFLAGS = $(TEST_CFLAGS)
testdb_SOURCES = testdb.c
testdb_LDADD = $(TEST_LDADD)

testexpression_CFLAGS = $(TEST_CFLAGS)
testexpression_SOURCES = testexpression.c
testexpression_LDADD = $(TEST_LDADD)

testinstance_CFLAGS = $(TEST_CFLAGS)
testinstance_SOURCES = testinstance.c
testinstance_LDADD = $(TEST_LDADD)

testlinear_CFLAGS = $(TEST_CFLAGS)
testlinear_SOURCES = testlinear.c
testlinear_LDADD = $(TEST_LDADD)

testmany_CFLAGS = $(TEST_CFLAGS)
testmany_SOURCES = testmany.c
testmany_LDADD = $(TEST_LDADD)

testmonitor_CFLAGS = $(TEST_CFLAGS)
testmonitor_SOURCES = testmonitor.c
testmonitor_LDADD = $(TEST_LDADD)

testparams_CFLAGS = $(TEST_CFLAGS)
testparams_SOURCES = testparams.c
testparams_LDADD = $(TEST_LDADD)

testparser_CFLAGS = $(TEST_CFLAGS)
testparser_SOURCES = testparser.c
testparser_LDADD = $(TEST_LDADD)

testprops_CFLAGS = $(TEST_CFLAGS)
testprops_SOURCES = testprops.c
testprops_LDADD = $(TEST_LDADD)

testqueue_CFLAGS = $(TEST_CFLAGS)
testqueue_SOURCES = testqueue.c
testqueue_LDADD = $(TEST_LDADD)

testquery_CFLAGS = $(TEST_CFLAGS)
testquery_SOURCES = testquery.c
testquery_LDADD = $(TEST_LDADD)

testrate_CFLAGS = $(TEST_CFLAGS)
testrate_SOURCES = testrate.c
testrate_LDADD = $(TEST_LDADD)

testrecv_CFLAGS = $(TEST_CFLAGS)
testrecv_SOURCES = testrecv.c
testrecv_LDADD = $(TEST_LDADD)

testreverse_CFLAGS = $(TEST_CFLAGS)
testreverse_SOURCES = testreverse.c
testreverse_LDADD = $(TEST_LDADD)

testselect_CFLAGS = $(TEST_CFLAGS)
testselect_SOURCES = testselect.c
testselect_LDADD = $(TEST_LDADD)

testsend_CFLAGS = $(TEST_CFLAGS)
testsend_SOURCES = testsend.c
testsend_LDADD = $(TEST_LDADD)

testsignals_CFLAGS = $(TEST_CFLAGS)
testsignals_SOURCES = testsignals.c
testsignals_LDADD = $(TEST_LDADD)

testspeed_CFLAGS = $(TEST_CFLAGS)
testspeed_SOURCES = testspeed.c
testspeed_LDADD = $(TEST_LDADD)

testsync_CFLAGS = $(TEST_CFLAGS)
testsync_SOURCES = testsync.c
testsync_LDADD = $(TEST_LDADD)

testvector_CFLAGS = $(TEST_CFLAGS)
testvector_SOURCES = testvector.c
testvector_LDADD = $(TEST_LDADD)

tests: all
	for i in $(test_all_ordered); do echo Running $$i; if ! ./$$i -qt; then exit 1; fi; done
	echo Running testmonitor and testsignals; ./testmonitor -t & ./testsignals

memtest: all
	for i in $(noinst_PROGRAMS); do echo Running $$i; if ! LD_PRELOAD=/usr/local/lib/libmapper-0.2.dylib valgrind --leak-check=full ./.libs/$$i -qt; then exit 1; fi; done
