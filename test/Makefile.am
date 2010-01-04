
noinst_PROGRAMS = testadmin testsend testrecv testlinear

testadmin_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS)
testadmin_SOURCES = testadmin.c
testadmin_LDADD = $(top_srcdir)/src/libmapper-@MAJOR_VERSION@.la $(liblo_LIBS)

testsend_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS)
testsend_SOURCES = testsend.c
testsend_LDADD = $(top_srcdir)/src/libmapper-@MAJOR_VERSION@.la $(liblo_LIBS)

testrecv_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS)
testrecv_SOURCES = testrecv.c
testrecv_LDADD = $(top_srcdir)/src/libmapper-@MAJOR_VERSION@.la $(liblo_LIBS)

testlinear_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS)
testlinear_SOURCES = testlinear.c
testlinear_LDADD = $(top_srcdir)/src/libmapper-@MAJOR_VERSION@.la $(liblo_LIBS)
