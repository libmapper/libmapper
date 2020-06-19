
noinst_HEADERS = mpr_internal.h types_internal.h
EXTRA_DIST = libmpr.def

if WINDOWS_DLL
lt_windows = -no-undefined -export-symbols $(srcdir)/libmpr.def
endif

lib_LTLIBRARIES = libmpr.la
libmpr_la_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS)
libmpr_la_SOURCES = device.c expression.c graph.c link.c list.c map.c \
    network.c object.c properties.c router.c signal.c slot.c table.c time.c \
    value.c
libmpr_la_LIBADD = $(liblo_LIBS)
libmpr_la_LDFLAGS = $(lt_windows) -export-dynamic -version-info @SO_VERSION@
