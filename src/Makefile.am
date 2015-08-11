
noinst_HEADERS = mapper_internal.h types_internal.h
EXTRA_DIST = libmapper.def

if WINDOWS_DLL
lt_windows = -no-undefined -export-symbols libmapper.def
endif

lib_LTLIBRARIES = libmapper-@MAJOR_VERSION@.la
libmapper_@MAJOR_VERSION@_la_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS)
libmapper_@MAJOR_VERSION@_la_SOURCES = network.c db.c device.c expression.c list.c \
    map.c monitor.c params.c router.c signal.c table.c time.c
libmapper_@MAJOR_VERSION@_la_LIBADD = $(liblo_LIBS)
libmapper_@MAJOR_VERSION@_la_LDFLAGS = $(lt_windows) -export-dynamic -version-info @SO_VERSION@
