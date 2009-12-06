
bin_PROGRAMS = testmapper test2

testmapper_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS)
testmapper_SOURCES = testmapper.c
testmapper_LDADD = $(top_srcdir)/src/libmapper-@MAJOR_VERSION@.la $(liblo_LIBS)

test2_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS)
test2_SOURCES = test2.c
test2_LDADD = $(top_srcdir)/src/libmapper-@MAJOR_VERSION@.la $(liblo_LIBS)
