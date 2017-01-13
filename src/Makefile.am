
noinst_HEADERS = mapper_internal.h types_internal.h
EXTRA_DIST = libmapper.def

if WINDOWS_DLL
lt_windows = -no-undefined -export-symbols libmapper.def
endif

lib_LTLIBRARIES = libmapper.la
libmapper_la_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS)
libmapper_la_SOURCES = database.c device.c expression.c link.c \
    list.c map.c network.c properties.c router.c signal.c slot.c table.c timetag.c
libmapper_la_LIBADD = $(liblo_LIBS)
libmapper_la_LDFLAGS = $(lt_windows) -export-dynamic -version-info @SO_VERSION@
