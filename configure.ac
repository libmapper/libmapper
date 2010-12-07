AC_PREREQ(2.61)

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
AC_CONFIG_MACRO_DIR([m4])

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
AC_CHECK_FUNC([inet_ntoa],[],[AC_ERROR([This is not a POSIX system!])])
AC_CHECK_FUNC([gettimeofday],[AC_DEFINE([HAVE_GETTIMEOFDAY],[],[Define if gettimeofday() is available.])],
              [AC_ERROR([This is not a POSIX system!])])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T

# pthreads
AX_PTHREAD

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_CC_C99
AC_PROG_CXX

# libtool
AM_PROG_LIBTOOL

# Check options
AC_ARG_ENABLE(debug,
   [AS_HELP_STRING([--enable-debug],[compile with debug flags])],
   enable_debug=yes)

AC_ARG_ENABLE(tests,
   [  --disable-tests         don't build test programs.],,
   enable_tests=yes)

AC_ARG_ENABLE(docs,
   [  --disable-docs          don't build the documentation.],,
   enable_docs=yes)

AC_ARG_ENABLE(audio,
   [  --disable-audio         don't build the audio examples.],,
   enable_audio=yes)

swig_enabled=yes
AC_ARG_ENABLE(swig,
   [  --disable-swig          don't build the SWIG bindings.],
   swig_enabled=$enableval)

if test x$swig_enabled = xyes; then
   AC_CHECK_PROG([SWIG], [swig], [swig])
   if test x$SWIG = x; then
     swig_enabled=no
     swig_explain="(swig not found)"
   else
     AM_PATH_PYTHON(2.3, [have_python="yes"], [have_python="no"])
     if test x$have_python = xyes; then
       AC_CHECK_PROG([PYCONFIG], [python-config], [python-config])
       if test x$PYCONFIG = x; then
         swig_enabled=no
         swig_explain="(python-config not found)"
       else
         PYCFLAGS=$(python-config --cflags)
         PYLIBS=$(python-config --libs)
         AC_SUBST(SWIG)
       fi
     else
       swig_enabled=no
       swig_explain="(python not found)"
     fi
   fi
fi

# Check for audio libraries supported by RtAudio
if test x$enable_audio = xyes; then

  # Try ALSA
  AC_CHECK_LIB(asound, snd_ctl_open,
    [RTAUDIO_CFLAGS="-D__LINUX_ALSA__ -D__LITTLE_ENDIAN__"
     RTAUDIO_LIBS=-lasound
     audio_explain="(ALSA)"])

  # Look for CoreAudio (This could be more sophisticated)
  echo -n Checking for CoreAudio...
  if test -e /System/Library/Frameworks/CoreAudio.framework; then
    RTAUDIO_CFLAGS="-D__MACOSX_CORE__ -D__LITTLE_ENDIAN__"
    RTAUDIO_LIBS="-framework CoreAudio -framework CoreFoundation"
    audio_explain="(CoreAudio)"
    echo yes
  else
    echo no
  fi

  if test -z "$RTAUDIO_CFLAGS"; then
    enable_audio=no
    audio_explain="(no supported audio system found.)"
  fi
fi

AC_SUBST(RTAUDIO_CFLAGS)
AC_SUBST(RTAUDIO_LIBS)

# Doxygen
if test x$enable_docs = xyes; then
  AC_CHECK_PROG([DOXYGEN], [doxygen], [doc], [])
  AC_SUBST(DOXYGEN)
  if test x$DOXYGEN = x; then
    enable_docs=no
    docs_explain="(doxygen not found.)"
  fi
fi

# pkg-config
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

  tmpLIBS="$LIBS"
  LIBS="$LIBS $liblo_LIBS"
  AC_CHECK_FUNC([lo_address_set_iface],
    [AC_DEFINE([HAVE_LIBLO_SET_IFACE],[],[Define to use lo_address_set_iface function in liblo.])])
  LIBS="$tmpLIBS"
])

# Debug mode
AS_IF([test x$enable_debug = xyes],
      [CFLAGS="-g -O0 -Wall -Werror -DDEBUG"],
      [CFLAGS="$CFLAGS -DNDEBUG"])

AM_CONDITIONAL(TESTS, test x$enable_tests = xyes)
AM_CONDITIONAL(HAVE_DOXYGEN, test "x$DOXYGEN" != x)
AM_CONDITIONAL(HAVE_PYTHON, test "x$have_python" = xyes)
AM_CONDITIONAL(HAVE_SWIG, test "x$swig_enabled" = xyes)

AC_CONFIG_FILES([
    Makefile
    src/Makefile
    include/Makefile
    test/Makefile
    doc/Makefile
    doc/libmapper.doxyfile
    swig/Makefile
    swig/setup.py
    examples/Makefile
    examples/pwm_synth/Makefile
    examples/py_tk_gui/Makefile
    examples/py_tk_gui/setup.py
    libmapper.pc
])
AC_OUTPUT

# Messages to user.
echo
echo libmapper configured:
echo --------------------------------------------------
echo "building documention...    " $enable_docs $docs_explain
echo "building tests...          " $enable_tests
echo "building SWIG bindings...  " $swig_enabled $swig_explain
echo "building audio examples... " $enable_audio $audio_explain
AS_IF([test x$enable_debug = xyes],
      [echo "Debug flags enabled."])
echo --------------------------------------------------
