
noinst_HEADERS = bitflags.h \
    device.h \
    expression.h \
    expression/expr_buffer.h \
    expression/expr_constant.h \
    expression/expr_evaluator.h \
    expression/expr_function.h \
    expression/expr_lexer.h \
    expression/expr_operator.h \
    expression/expr_parser.h \
    expression/expr_stack.h \
    expression/expr_struct.h \
    expression/expr_token.h \
    expression/expr_trace.h \
    expression/expr_value.h \
    expression/expr_variable.h \
    graph.h \
    id.h \
    id_map.h \
    link.h \
    list.h \
    map.h \
    message.h \
    mpr_signal.h \
    mpr_time.h \
    mpr_type.h \
    network.h \
    object.h \
    path.h \
    property.h \
    slot.h \
    table.h \
    thread_data.h \
    value.h \
    util/mpr_debug.h \
    util/mpr_inline.h \
    util/mpr_set_coerced.h
EXTRA_DIST = libmapper.def

if WINDOWS_DLL
lt_windows = -no-undefined -export-symbols $(srcdir)/libmapper.def
endif

lib_LTLIBRARIES = libmapper.la
libmapper_la_CFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS)
libmapper_la_SOURCES = device.c \
    expression.c \
    graph.c \
    link.c \
    list.c \
    map.c \
    message.c \
    network.c \
    object.c \
    path.c \
    property.c \
    signal.c \
    slot.c \
    table.c \
    time.c \
    util/mpr_set_coerced.c \
    value.c
libmapper_la_LIBADD = $(liblo_LIBS)
libmapper_la_LDFLAGS = $(lt_windows) -export-dynamic -version-info @SO_VERSION@
