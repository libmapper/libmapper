Note: This is an on-going document to describe the set-up procedure
for compiling libmapper under windows. It is not done yet, sorry.  If
you use Windows, please feel free to help.

How to compile libmapper on Microsoft Windows
=============================================

Since libmapper uses open source tools for its build system, the
following instructions must be followed to set up a development
environment on a Microsoft Windows operating system:

Install MingW
-------------

mingw-get-inst

Install Python
--------------

### Download and install

### Set up distutils for mingw32

boodebr.org/main/python/build-windows-extensions

distutils.cfg:

    [build]
    compiler = mingw32

Install autotools via mingw-get
-------------------------------

Get pkg-config
--------------

glib, pkg-config

Get source for liblo and libmapper
----------------------------------


How to compile libmapper for Windows using Linux
================================================

Another way to produce the Windows build is to use MingW from a Linux
host under a cross-compiler configuration.  Here are the arguments
needed to tell autotools to do this:

    ./configure --host i586-mingw32msvc --prefix=$HOME/.win \
                --disable-audio --disable-jni --disable-docs \
                CFLAGS="-DWIN32 -D_WIN32_WINNT=0x501" \
                LDFLAGS="-L$HOME/.win/lib" \
                LIBS="-lws2_32 -liphlpapi -lpthread"

As you can see, we create an install target in `$HOME/.win` to hold
the necessary headers and libraries for the Windows build.  We also
tell it to use the MingW32 compiler.  We disable audio and Java,
although you can try to build Java bindings if you have the necessary
headers and the JDK available.

You should have a Windows version of Python also installed in the
`.win` prefix, otherwise I recommend adding `--disable-swig`.
