AC_PREREQ(2.63)

AC_INIT([libmapper],[0.1],[sinclair@music.mcgill.ca])

# libtool version: current:revision:age
#
# If the library source code has changed at all since the last update, then
# increment revision (`c:r:a' becomes `c:r+1:a').
#
# If any interfaces have been added, removed, or changed since the last update,
# increment current, and set revision to 0.
#
# If any interfaces have been added since the last public release, then
# increment age.
#
# If any interfaces have been removed since the last public release, then set
# age to 0.
SO_VERSION=1:0:0

AC_CONFIG_SRCDIR([src/device.c])
AC_CONFIG_HEADERS([src/config.h])

AM_INIT_AUTOMAKE([foreign])
AM_MAINTAINER_MODE
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES])

AC_ENABLE_STATIC(no)
AC_ENABLE_SHARED(yes)
AC_SUBST(SO_VERSION)

MAJOR_VERSION=$(echo $PACKAGE_VERSION | cut -d. -f1)
AC_SUBST(MAJOR_VERSION)

AM_SANITY_CHECK

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([sys/time.h unistd.h termios.h fcntl.h errno.h])
AC_CHECK_HEADERS([arpa/inet.h])
AC_CHECK_FUNC([inet_ntoa])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T

# Checks for programs.
AC_PROG_CC
LT_INIT
AM_PROG_CC_C_O

# Check options
AC_ARG_ENABLE(tests,
   [  --disable-tests         don't build test programs.],,
   enable_tests=yes)

AC_ARG_ENABLE(docs,
   [  --disable-docs          don't build the documentation.],
   enable_docs=)

swig_enabled=yes
AC_ARG_ENABLE(swig,
   [  --disable-swig          don't build the SWIG bindings.],
   swig_enabled=$enableval)

if test x$swig_enabled = xyes; then
   AM_PATH_PYTHON(2.3, [have_python="yes"], [have_python="no"])
   AC_PROG_SWIG(1.3.36)
   SWIG_PYTHON
fi

# Doxygen
AC_CHECK_PROG([DOXYGEN], [doxygen], [doc], [])
AC_SUBST(DOXYGEN)
PKG_PROG_PKG_CONFIG

# Check for liblo
AC_ARG_WITH([liblo],
  AS_HELP_STRING([--without-liblo],[compile without liblo, disable OSC]))
AS_IF([test x$with_liblo != xno],[
  PKG_CHECK_MODULES([liblo], [liblo >= 0.26])])
AS_IF([test "x$liblo_LIBS" = x],
  [with_liblo=no])
AS_IF([test x$with_liblo != xno],[
  AC_DEFINE([HAVE_LIBLO],[],[Define to enable Open Sound Control support with liblo])
  AC_SUBST(LIBLO,liblo)
])

if test "x$SWIG" = x || test "x$swig_enabled" = xno; then
   SWIG=
fi

AM_CONDITIONAL(TESTS, test x$enable_tests = xyes)
AM_CONDITIONAL(HAVE_DOXYGEN, test "x$DOXYGEN" != x)
AM_CONDITIONAL(HAVE_PYTHON, test "x$have_python" = xyes)
AM_CONDITIONAL(HAVE_SWIG, test "x$SWIG" != x)

if test "x$DOXYGEN" = x; then
   enable_docs=no
else
   enable_docs=yes
fi

AC_CONFIG_FILES([
    Makefile
    src/Makefile
    include/Makefile
    test/Makefile
    libmapper.pc
])
AC_OUTPUT
