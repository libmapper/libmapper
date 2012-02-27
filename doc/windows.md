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
