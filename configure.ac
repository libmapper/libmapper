AC_PREREQ(2.61)

m4_define([LIBMAPPER_VERSION],[2.4])
AC_INIT([libmapper],
  m4_esyscmd_s([test -d .git && (git describe --tags | sed 's/\([^\-]*\)-\([^\-]*\)-\([^\-]*\)/\1.\2+\3/g') || echo LIBMAPPER_VERSION]),
  [dot_mapper@googlegroups.com],[],[http://libmapper.org])

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
SO_VERSION=11:1:0

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
AC_CHECK_HEADERS([zlib.h])
AC_CHECK_HEADERS([winsock2.h])
AC_CHECK_HEADERS([inttypes.h])
AC_CHECK_FUNC([inet_ptoa],[AC_DEFINE([HAVE_INET_PTOA],[],[Define if inet_ptoa() is available.])],[])
AC_CHECK_FUNC([getifaddrs],[AC_DEFINE([HAVE_GETIFADDRS],[],[Define if getifaddrs() is available.])],[
  AC_CHECK_LIB([iphlpapi],[exit],[
      # Need some functions not available before Windows XP
      CFLAGS="$CFLAGS -DWIN32 -D_WIN32_WINNT=0x501"
      CXXFLAGS="$CXXFLAGS -DWIN32 -D_WIN32_WINNT=0x501"
      AC_DEFINE([HAVE_LIBIPHLPAPI],[],[Define if iphlpapi library is available. (Windows)])
      is_windows=yes
    ],[])])
AC_CHECK_FUNC([gettimeofday],[AC_DEFINE([HAVE_GETTIMEOFDAY],[],[Define if gettimeofday() is available.])],
              [AC_MSG_ERROR([This is not a POSIX system!])])

AC_CHECK_LIB([z], [gzread], , [AC_MSG_ERROR([zlib not found, see http://www.zlib.net])])

AM_CONDITIONAL(WINDOWS, test x$is_windows = xyes)
AM_CONDITIONAL(WINDOWS_DLL, test x$is_windows = xyes && test x$enable_shared = xyes)

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_CC_C99
AC_PROG_CXX

# libtool
AC_LIBTOOL_WIN32_DLL
AM_PROG_LIBTOOL

# Check for the math library
# (Does not survive -Werror!)
tmpCFLAGS="$CFLAGS"
CFLAGS=$(echo $CFLAGS | tr ' ' '\n' | grep -v -- -Werror)
LT_LIB_M
LIBS="$LIBS $LIBM"
CFLAGS="$tmpCFLAGS"

# Check for C++11 features
_CXXFLAGS="$CXXFLAGS"
AC_LANG_PUSH([C++])

AC_MSG_CHECKING([whether C++11 lambdas are supported])
CXXFLAGS="$_CXXFLAGS -std=c++11"
AC_COMPILE_IFELSE(
  [AC_LANG_PROGRAM([#include <functional>],[[[]](std::function<void()> f){f();}([[]](){0;});])],
    [AC_MSG_RESULT([yes])
     HAVE_LAMBDA=yes],
    [
CXXFLAGS="$_CXXFLAGS -std=c++0x"
AC_COMPILE_IFELSE(
  [AC_LANG_PROGRAM([#include <functional>],[[[]](std::function<void()> f){f();}([[]](){0;});])],
    [AC_MSG_RESULT([yes])
     HAVE_LAMBDA=yes],
    [
CXXFLAGS="$_CXXFLAGS -std=c++11 -stdlib=libc++"
AC_COMPILE_IFELSE(
  [AC_LANG_PROGRAM([#include <functional>],[[[]](std::function<void()> f){f();}([[]](){0;});])],
    [AC_MSG_RESULT([yes])
     HAVE_LAMBDA=yes],
    [AC_MSG_RESULT([no])
     CXXFLAGS="$_CXXFLAGS"])])])
AM_CONDITIONAL([HAVE_LAMBDA],[test x$HAVE_LAMBDA = xyes])

# If we can add -Qunused-arguments, add it.
# This error occurs when ccache and clang are used together.
AC_MSG_CHECKING([whether to add -Qunused-arguments (C++)])
_CXXFLAGS="$CXXFLAGS"
CXXFLAGS="$_CXXFLAGS -Qunused-arguments"
AC_COMPILE_IFELSE(
  [AC_LANG_PROGRAM([],[])],
  [AC_MSG_RESULT([yes])],
  [AC_MSG_RESULT([no])
   CXXFLAGS="$_CXXFLAGS"])
AC_LANG_POP([C++])

# Check options
AC_ARG_ENABLE(debug,
   [AS_HELP_STRING([--enable-debug],[compile with debug flags])],
   enable_debug=$enableval)

AC_ARG_ENABLE(tests,
   [  --disable-tests         don't build test programs.],,
   enable_tests=$enableval, enable_tests=yes)

AC_ARG_ENABLE(docs,
   [  --disable-docs          don't build the documentation.],,
   enable_docs=$enableval, enable_docs=yes)

AC_ARG_ENABLE(audio,
   [  --disable-audio         don't build the audio examples.],,
   enable_audio=$enableval, enable_audio=yes)

AC_ARG_ENABLE(python,
   [  --disable-python        don't build the Python bindings.],
   enable_python=$enableval=$enableval, enable_python=yes)

AC_ARG_ENABLE(java,
   [  --disable-java          don't build the Java JNI bindings.],
   enable_jni=$enableval, enable_jni=yes)

AC_ARG_ENABLE(csharp,
   [  --disable-csharp        don't build the C# bindings.],
   enable_csharp=$enableval, enable_csharp=yes)

AC_ARG_ENABLE(threads,
   [  --disable-threads       don't build with threading support.],
   enable_threads=$enableval, enable_threads=yes)

# Check if win32 threads are wanted
AC_ARG_WITH(win32-threads,
   [  --with-win32-threads    Use win32 threads], [], [with_win32_threads=yes])
if test "x$enable_threads" = xyes; then
   AC_CHECK_HEADER([winsock2.h], [HAVE_WINSOCK2_H=yes])
   if test "x$HAVE_WINSOCK2_H" = xyes; then
     if test "x$with_win32_threads" = xyes; then
       have_threads="yes"
     fi
   fi
fi

# Check for whether threads are wanted, and if so, did we find libpthread.
if test "x$enable_threads" = "xyes"; then
  if test "x$with_win32_threads" = "xyes" && test "x$have_threads" = "xyes"; then
    AC_DEFINE([HAVE_WIN32_THREADS],[1],[Define to use the win32 library for threading.])
    threads_explain="(win32 threads)"
  else
  AC_SEARCH_LIBS([pthread_create], [c pthread],
    [AS_IF([! test x"$ac_res" = x"-lc" && ! test x"$ac_res" = x"none required"],
           [LIBPTHREAD="$ac_res"])
     have_threads=yes;
     HAVE_LIBPTHREAD=1;
     threads_explain="(pthread)"
     AC_DEFINE([HAVE_LIBPTHREAD],[1],[Define to use the pthread library for threading.])],
     AC_MSG_ERROR([pthread not found. Try option --disable-threads.]))
  fi
  AC_DEFINE(ENABLE_THREADS, [1], [Define this to enable threads.])
fi

if test x$enable_python = xyes; then
   AM_PATH_PYTHON(2.3, [have_python="yes"], [have_python="no"])
   if test x$have_python = xyes; then
     PYEXT=$($PYTHON -c "import sys; print({'win32':'pyd','darwin':'so','linux':'so','linux2':'so'}[[sys.platform]])")
     AC_SUBST(PYEXT)
     case "$PYTHON_VERSION" in
     3*) PY3=" -py3"; AC_SUBST(PY3)
     esac
     python_explain="($(basename $PYTHON)$PY3)"
     AC_CONFIG_LINKS([bindings/python/libmapper/__init__.py:bindings/python/libmapper/__init__.py])
   else
     enable_python=no
     python_explain="(python not found)"
   fi
fi

# Check for JNI
if test x$enable_jni = xyes; then
   AC_MSG_CHECKING([JDK path])
   if test x"$JAVA_HOME" == "x"; then
      JAVA_HOME="$(dirname $(dirname $(readlink /etc/alternatives/javac 2>/dev/null)))"
   fi
   if test x"$JAVA_HOME" != "x" && test -d "$JAVA_HOME/include"; then
      JDKPATH="$JAVA_HOME"
   elif test -d /usr/lib/jvm/default-java/include; then
      JDKPATH=/usr/lib/jvm/default-java
   elif test -d /System/Library/Frameworks/JavaVM.framework/Headers; then
      JDKPATH=/System/Library/Frameworks/JavaVM.framework
   fi

   AC_ARG_WITH([jdk-path],
     AS_HELP_STRING([--with-jdk-path],[specify the path to the JDK]),
     [JDKPATH="$withval"])

   AC_MSG_RESULT([$JDKPATH])

   if test x"$JNIPATH" = x; then
     if test -f "$JDKPATH"/Headers/jni.h; then
       JNIPATH="$JDKPATH"/Headers
     elif test -f "$JDKPATH"/include/jni.h; then
       JNIPATH="$JDKPATH"/include
     fi
     JNIFLAGS="-I$JNIPATH"
     case $host_os in
       linux*)
         if test -d "$JNIPATH"/linux; then
           JNIFLAGS="$JNIFLAGS -I$JNIPATH/linux"
         fi;;
       darwin*)
         if test -d "$JNIPATH"/darwin; then
           JNIFLAGS="$JNIFLAGS -I$JNIPATH/darwin"
         fi;;
     esac
   fi
   if test x"$JDKBINPATH" = x; then
     if test -f "$JDKPATH"/Commands/javac; then
       JDKBINPATH="$JDKPATH"/Commands
     elif test -f "$JDKPATH"/bin/javac; then
       JDKBINPATH="$JDKPATH"/bin
     fi
   fi

   AC_PATH_PROG([JAVAC], [javac], [], ["$JDKBINPATH"$PATH_SEPARATOR$PATH])
   AC_PATH_PROG([JAR],   [jar],   [], ["$JDKBINPATH"$PATH_SEPARATOR$PATH])
   if test x$JAVAC = x; then
     enable_jni=no
     jni_explain="(javac not found)"
   elif test x$JAR = x; then
     enable_jni=no
     jni_explain="(jar not found)"
   else
     CFLAGS_revert="$CFLAGS"
     CFLAGS="$CFLAGS $JNIFLAGS"
     AC_CHECK_HEADER([jni.h],
       [enable_jni=yes],
       [enable_jni=no; jni_explain="(jni.h not found)"])
     CFLAGS="$CFLAGS_revert"
     if test x$enable_jni = xyes; then
       JNI=bindings/java
       AC_SUBST(JNIFLAGS)
       AC_SUBST(JNI)
       AC_CONFIG_LINKS([bindings/java/TestInstances/TestInstances.pde:bindings/java/TestInstances/TestInstances.pde])
     fi
   fi
fi

if test x$enable_csharp = xyes; then
   AC_CHECK_PROG([CSC], [csc], [csc])
   if test x$CSC = x; then
     enable_csharp=no
     csharp_explain="(csc not found)"
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

# Library filename extension
if test x$is_windows = xyes; then
  SO_EXT=dll
else
  case "$(uname -s)" in
    *arwin*) SO_EXT=dylib;;
    *) SO_EXT=so;;
  esac
fi
AC_SUBST(SO_EXT)

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
  PKG_CHECK_MODULES([liblo], [liblo >= 0.30])])
AS_IF([test "x$liblo_LIBS" = x],
  [with_liblo=no])
AS_IF([test x$with_liblo != xno],[
  AC_DEFINE([HAVE_LIBLO],[],[Define to enable Open Sound Control support with liblo])
  AC_SUBST(LIBLO,liblo)

  tmpLIBS="$LIBS"
  LIBS="$LIBS $liblo_LIBS $LIBM"
  AC_CHECK_FUNC([lo_address_set_iface],
    [AC_DEFINE([HAVE_LIBLO_SET_IFACE],[],[Define to use lo_address_set_iface function in liblo.])])
  AC_CHECK_FUNC([lo_server_new_multicast_iface],
    [AC_DEFINE([HAVE_LIBLO_SERVER_IFACE],[],[Define to use lo_server_new_multicast_iface function in liblo.])])
  AC_CHECK_FUNC([lo_bundle_count],
    [AC_DEFINE([HAVE_LIBLO_BUNDLE_COUNT],[],[Define to use lo_bundle_count function in liblo.])])
  AC_CHECK_FUNC([lo_servers_recv_noblock],
    [AC_DEFINE([HAVE_LIBLO_SERVERS_RECV_NOBLOCK],[],[Define to use lo_servers_recv_noblock function in liblo.])],[AC_MSG_ERROR([liblo >= 0.30 required.])])
  LIBS="$tmpLIBS"
])

# If shared is disabled, need to link bindings directly to zlib
if test x$enable_shared != xyes; then
  EXTRA_LIBS_BINDINGS=\"z\"
fi
AC_SUBST(EXTRA_LIBS_BINDINGS)

# Debug mode
AS_IF([test x$enable_debug = xyes],
      [CFLAGS="-g -O0 -Wall -Werror -DDEBUG `echo $CFLAGS | sed 's/-O2//'`"
       CXXFLAGS="-g -O0 -Wall -Werror -DDEBUG `echo $CXXFLAGS | sed 's/-O2//'`"],
      [CFLAGS="$CFLAGS -DNDEBUG"; CXXFLAGS="$CXXFLAGS -DNDEBUG"])

# Add -I. so that config.h is found correctly during VPATH builds
# (see autoconf manual section 4.9)
CFLAGS="-I. $CFLAGS"
CXXFLAGS="-I. $CXXFLAGS"

AM_CONDITIONAL(TESTS, test "x$enable_tests" = xyes)
AM_CONDITIONAL(HAVE_DOXYGEN, test "x$DOXYGEN" != x)
AM_CONDITIONAL(HAVE_PYTHON, test "x$enable_python" = xyes)
AM_CONDITIONAL(HAVE_CSHARP, test "x$enable_csharp" = xyes)
AM_CONDITIONAL(HAVE_JAVA, test "x$enable_jni" = xyes)
AM_CONDITIONAL(HAVE_AUDIO, test "x$enable_audio" = xyes)
AM_CONDITIONAL(ENABLE_THREADS, test "x$enable_threads" = xyes)

AM_COND_IF([HAVE_CSHARP], [AC_CONFIG_FILES(bindings/csharp/Makefile)])
AM_COND_IF([HAVE_PYTHON], [AC_CONFIG_FILES(bindings/python/Makefile bindings/python/setup.py bindings/python/libmapper/mapper.py)])
AM_COND_IF([HAVE_JAVA], [AC_CONFIG_FILES(bindings/java/Makefile bindings/java/mapper/NativeLib.java)])

AC_CONFIG_FILES([
    Makefile
    src/Makefile
    include/Makefile
    test/Makefile
    doc/Makefile
    doc/libmapper.doxyfile
    bindings/Makefile
    examples/Makefile
    examples/pwm_synth/Makefile
    extra/Makefile
    extra/macOS/Makefile
    libmapper.pc
])
AC_OUTPUT

# Messages to user.
echo
echo libmapper configured:
echo --------------------------------------------------
echo "building documentation...   " $enable_docs $docs_explain
echo "building tests...           " $enable_tests
echo "threading support...        " $enable_threads $threads_explain
echo "building Python bindings... " $enable_python $python_explain
echo "building Java bindings...   " $enable_jni $jni_explain
echo "building C# bindings...     " $enable_csharp $csharp_explain
echo "building audio examples...  " $enable_audio $audio_explain
AS_IF([test x$enable_debug = xyes],
      [echo "Debug flags enabled."])
echo "version                     " $PACKAGE_VERSION
echo --------------------------------------------------
