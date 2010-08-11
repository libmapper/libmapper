
TEST_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS)
TEST_LDADD = $(top_srcdir)/src/libmapper-@MAJOR_VERSION@.la $(liblo_LIBS)

noinst_PROGRAMS = test testadmin testsend testrecv testexpression	\
                  testlinear testparams testmonitor testdb

testadmin_CFLAGS = $(TEST_CFLAGS)
testadmin_SOURCES = testadmin.c
testadmin_LDADD = $(TEST_LDADD)

testsend_CFLAGS = $(TEST_CFLAGS)
testsend_SOURCES = testsend.c
testsend_LDADD = $(TEST_LDADD)

testrecv_CFLAGS = $(TEST_CFLAGS)
testrecv_SOURCES = testrecv.c
testrecv_LDADD = $(TEST_LDADD)

testlinear_CFLAGS = $(TEST_CFLAGS)
testlinear_SOURCES = testlinear.c
testlinear_LDADD = $(TEST_LDADD)

testexpression_CFLAGS = $(TEST_CFLAGS)
testexpression_SOURCES = testexpression.c
testexpression_LDADD = $(TEST_LDADD)

testparams_CFLAGS = $(TEST_CFLAGS)
testparams_SOURCES = testparams.c
testparams_LDADD = $(TEST_LDADD)

testmonitor_CFLAGS = $(TEST_CFLAGS)
testmonitor_SOURCES = testmonitor.c
testmonitor_LDADD = $(TEST_LDADD)

testdb_CFLAGS = $(TEST_CFLAGS)
testdb_SOURCES = testdb.c
testdb_LDADD = $(TEST_LDADD)

test_CFLAGS = $(TEST_CFLAGS)
test_SOURCES = test.c
test_LDADD = $(TEST_LDADD)
