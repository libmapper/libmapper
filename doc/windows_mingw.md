# Cross-compiling libmapper for Microsoft Windows using MinGW (DEPRECATED)

Since libmapper uses open source tools for its build system, the
following instructions must be followed to set up a development
environment on a Microsoft Windows operating system:

## Install MinGW

Download and install [MinGW](http://sourceforge.net/projects/mingw/). Also install the following packages:

* mingw32-autotools
* mingw32-gcc
* mingw32-gcc-g++
* mingw32-binutils
* mingw32-mingw-utils
* mingw32-w32api
* mingw32-autotools
* mingw32-pthreads-w32
* mingw32-libz

The following instructions will assume you installed MinGW in the folder `C:\MinGW`


## Install pkg-config and glib

* Download [pkg-config](http://ftp.gnome.org/pub/gnome/binaries/win32/dependencies/pkg-config_0.26-1_win32.zip)
* Extract the file `bin/pkg-config.exe` and move it to the folder `C:\MinGW\bin`
* Download [gettext-runtime](http://ftp.gnome.org/pub/gnome/binaries/win32/dependencies/gettext-runtime_0.18.1.1-2_win32.zip)
* Extract the file `bin/intl.dll` and move it to the folder `C:\MinGW\bin`
* Download [glib](http://ftp.gnome.org/pub/gnome/binaries/win32/glib/2.28)
* Extract the file `lib/libglib-2.0-0.dll` and move it to the folder `C:\MinGW\bin`


## Update libtool

MinGW currently includes libtool version 2.4, but we need version 2.4.2. Download and install [libtool 2.4.2](http://mirror-fr2.bbln.org/gnu/libtool/libtool-2.4.2.tar.gz)


## Install Python

Download and install [python](https://www.python.org/downloads/). Subsequent instructions will assumre that Python is installed in the folder `C:\Python`

### Set up distutils for MinGW

From an archived version of [boodebr.org/main/python/build-windows-extensions](https://web.archive.org/web/20120423102540/http://boodebr.org/main/python/build-windows-extensions)

You now have to tell Python to use the MinGW compiler when building extensions. To do this, create the file `C:\Python\Lib\distutils\distutils.cfg` with the following contents:

    [build]
    compiler = mingw32

## Get source for liblo and libmapper

* Download and install [liblo](https://github.com/radarsat1/liblo). Warning: if you download the 0.28 release code instead of from the git repository there is a symbol missing from the file `src/liblo.def` (lo_server_enable_queue)
* Download [libmapper](http://libmapper.github.io/downloads.html)

Edit your PKG_CONFIG_PATH:

    $ export PKG_CONFIG_PATH=/local/lib/pkgconfig

## Build and install liblo and libmapper

    $ ./autogen.sh
    $ ./configure --host i586-mingw32msvc --prefix=$HOME/.win \
        CFLAGS="-DWIN32 -D_WIN32_WINNT=0x501" \
        LDFLAGS="-L$HOME/.win/lib" \
        LIBS="-lws2_32 -liphlpapi -lpthread"
    $ make
    $ sudo make install

For libmapper, also add the following flags:

    --disable-examples --disable-audio --disable-java --disable-docs

As you can see, we create an install target in `$HOME/.win` to hold
the necessary headers and libraries for the Windows build.  We also
tell it to use the MinGW32 compiler.  We disable audio and Java,
although you can try to build Java bindings if you have the necessary
headers and the JDK available.

You should have a Windows version of Python installed, and specify the
path to it in CFLAGS, if you want to build the Python bindings,
otherwise also provide `--disable-python`.

Note that the above makes a local folder for the install location for
Windows targets called `$HOME/.win`, which helps avoid mixing Windows
and Linux binaries.  LibLo requires that the ["win32" port of
pthreads][pthreadwin32] is found in your prefix location, so you
should compile and install that before proceeding.

[pthreadwin32]: http://sourceware.org/pthreads-win32

