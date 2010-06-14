
lib_LTLIBRARIES = libmapper-@MAJOR_VERSION@.la
libmapper_@MAJOR_VERSION@_la_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS)
libmapper_@MAJOR_VERSION@_la_SOURCES = operations.c expression.c interpreter.c device.c admin.c signal.c router.c mapping.c
libmapper_@MAJOR_VERSION@_la_LDFLAGS = -export-dynamic -version-info @SO_VERSION@

EXTRA_DIST = operation.h expression.h mapper_internal.h types_internal.h
