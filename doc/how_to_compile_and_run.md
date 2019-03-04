
Building libmapper
==================

This file documents the build process for libmapper for various
operating systems, and will be updated as the libmapper core project
progresses.

Linux and OS X
--------------

### Dependencies

libmapper depends version 0.27 of liblo or later.
Please consult the [LibLo project page][liblo] for details.

[liblo]: http://liblo.sourceforge.net

The GNU configure step detects liblo using the "pkg-config" program.
This is usually already installed in Linux systems, but on OS X, we
recommend installing it via [MacPorts][ports] or [HomeBrew][brew].  In
the future libmapper packages for MacPorts and HomeBrew will be
directly provided, but this is not yet the case.

[ports]: http://www.macports.org
[brew]: http://mxcl.github.com/homebrew

You may wish to manually check that the correct version of liblo is
detected, by running,

    pkg-config --libs --cflags liblo

If the path to liblo is not correct, please set up the
`PKG_CONFIG_PATH` environment variable appropriately and try again.

libmapper also has optional dependencies on the Java SDK as well as
SWIG and Doxygen, for Java and Python bindings, and documentation,
respectively.  The Java SDK may be installed according to your
standard operating system procedure, and you can check if it is
already installed by running,

    javac -version

These should print the installed version numbers of these programs if
correctly configured.  You may install `swig` and `doxygen` packages
using your preferred package manager.

The `examples` folder also contains an audio example, which binds to
`libasound` on Linux, and CoreAudio on Mac OS X.  On Ubuntu, you may
wish to installed `libasound-dev` package, or the corresponding
package on other Linux distributions.

### Configuring

If you have extracted libmapper from a release tarball, run,

    ./configure
    
to configure the software with default options.  If you are using
libmapper from a repository, you will need to run,

    ./autogen.sh
    
which will create the `configure` script and run it for you.  You can
optionally specify an install location with,

    ./configure --prefix=<location>

It is recommended to either use `/usr/local/` or a directory in your
home directory such as `$HOME/.local`.  Users of HomeBrew on OS X
should always use `/usr/local`.

If you will be hacking on libmapper or which to have verbose output
while it is running, we recommended enabling debug mode:

    ./configure --enable-debug

Additionally, Java and Python bindings, and audio examples, may be
disabled with options `--disable-jni`, `--disable-swig`, and
`--disable-audio` respectively.

After `configure` runs successfully, the configuration options will be
printed for your confirmation.  If anything unexpected occurs, be sure
to check `config.log` for information about what failed.

### Building

Once the build is configured, build it with,

    make

Should any errors occur, please inform the libmapper mailing list.

To verify that the library runs without errors, you may run the main
test program, available in the test directory:

    cd test
    ./test

Note that the programs in the test directory are _not_ examples of
library usage, as they additionally test some internal functions in
the library.  Please see the `examples` directory for examples of how
to use the library.

### Installing

The software may be installed with,

    make install

This should place headers in `<prefix>/include/mapper`, the library
in `<prefix>/lib`, Python bindings in
`<prefix>/lib/pythonXX/site-packages` (where XX is your Python
version), and a `pkg-config` information file in
`<prefix>/lib/pkgconfig`.

Once installation is successful, you can check that the library is
found by `pkg-config`:

    pkg-config --libs --cflags libmapper

Note that the Java bindings are not installed, as there is no standard
location in which to put them.  However, they can be copied to
wherever is convenient for the classpath and java.library.path in your
project.  Instructions specific to Processing.org can be found in a
dedicated section below.

### Testing

As mentioned, you can test libmapper by running the test program,

    test/test

This should create two devices, link them, connect their signals, and
update a value continuously.  The other device should report that it
received the value.

You may wish to observe the multicast "admin" traffic, which can be
done by watching the multicast UDP port for OSC data using liblo's
`oscdump` utility:

    oscdump 7570 224.0.1.3

You can also use a libmapper GUI, such as [webmapper][webmapper], to
see that the devices and signals are discovered correctly.  You can
use the GUI to modify the connection properties, and observe that the
received values are changed.

[webmapper]: http://github.com/radarsat1/webmapper

You should also test the Python and Java bindings, if you plan to use
these.  Some other programs, such as webmapper, may depend on them, so
it is recommended to do so.

To test that the Python module is working, it is generally enough to
run the following command,

    python -m mapper

This will import the `mapper` module, which will fail if either
`mapper.py` or the native portion of the binding are not found.  You
may need to adjust your `PYTHONPATH` variable to ensure these can be
found.

You can test running programs by `cd`'ing to the `swig` folder and
running,

    cd swig
    python test.py

or,

    python tkgui.py

for a GUI example which brings up a single, mappable slider.  Running
multiple copies of `tkgui.py`, you can try mapping one to another to
make sure libmapper is functional.

Similarly, the Java bindings may be tested by `cd`'ing to the `jni`
folder and running `test` with the correct class and library paths:

    cd jni
    java -cp libmapper.jar -Djava.library.path=.libs test

Cross-compiling for Windows under Linux
---------------------------------------

Since libmapper was developed on Unix-like systems (Linux and Apple's
OS X), building libmapper uses GNU command-line tools.  However, it is
possible to build it for the Microsoft Windows operating system using
the MingW cross-compiler under Linux, or by using MingW from Windows.

Please see the file `windows.md` for instructions on how to set up
your MingW environment and extra dependencies before compiling
libmapper.

Briefly, the secret sauce for compiling liblo and libmapper for
Windows under an Ubuntu Linux environment is to install the
`gcc-mingw32` package, and then provide the following arguments to
`configure`:

    ./configure --host i586-mingw32msvc --prefix=$HOME/.win \
        CFLAGS="-DWIN32 -D_WIN32_WINNT=0x501" \
        LDFLAGS="-L$HOME/.win/lib" \
        LIBS="-lws2_32 -liphlpapi -lpthread"

For libmapper, also add the following flags:

    --disable-examples --disable-audio --disable-jni --disable-docs

You should have a Windows version of Python installed, and specify the
path to it in CFLAGS, if you want to build the Python bindings,
otherwise also provide `--disable-swig`.

Note that the above makes a local folder for the install location for
Windows targets called `$HOME/.win`, which helps avoid mixing Windows
and Linux binaries.  LibLo requires that the ["win32" port of
pthreads][pthreadwin32] is found in your prefix location, so you
should compile and install that before proceeding.

[pthreadwin32]: http://sourceware.org/pthreads-win32

Problems areas and topics
-------------------------

Please remember that libmapper is still in a development and research
phase.  Although it is fairly robust at this point, since it is a
distributed, asynchronous system there are many pieces involved, and
supporting programs may have their own problems.  As always, if you
find a problem with libmapper or libmapper-enabled programs, please
consult the [mailing list][list].

Here, we address some common issues that new users encounter with the
core libmapper library.

### Architecture issues

We have found that in some cases, especially on Mac OS X, there are
programs that do not use the computer's native architecture.  For
example, Processing.org and Cycling 74's Max/MSP are 32-bit
applications, even if you are running a 64-bit version of OS X.
Therefore after building libmapper, it _will not work_ with these
programs.

We recommend that on OS X you build a universal binary, since we have
found it saves a lot of trouble later on.  Note that you must build
universal binaries of liblo as well as libmapper.

To do so, for both liblo and libmapper, you must perform the
`configure` and `make` steps with the following flags:

    ./configure CFLAGS="-arch i386 -arch x86_64" \
                CXXFLAGS="-arch i386 -arch x86_64" \
                --disable-dependency-tracking
    make

The `file` command should list both 32- and 64-bit architectures.

    file src/.libs/libmapper.6.dylib

(Of course, replace ".6" with the current libmapper version.)

Although we do not explicitly list them here, similar steps should be
performed for liblo, as well as for the native portions of the Python
bindings:

    file swig/.libs/_mapper.so

### Processing.org

Processing.org is a Java-based IDE and set of libraries for developing
visualizations and interactive art.  To use libmapper with Processing,
you should place the JNI bindings into the appropriate locations.

From the libmapper directory, create a directory called
`libraries/libmapper/library` under your sketchbook directory:

    mkdir -p <sketchbook>/libraries/libmapper/library

Now copy the JNI bindings to this directory, renaming the jar file to
match the name of the directory:

    cp jni/.libs/libmapperjni.6.dylib <sketchbook>/libraries/libmapper/library/libmapperjni.dylib
    cp jni/libmapper.jar <sketchbook>/libraries/libmapper/library/libmapper.jar

Create a file `export.txt`:

    echo 'name = libmapper' > "<sketchbook>/libraries/libmapper/library/export.txt"

Now, when you run the Processing IDE, you should see "libmapper"
listed under "Sketch/Import Library...".

Choosing this will insert two lines at the top of your sketch:

    import Mapper.*;
    import Mapper.Db.*;

You can test it by creating a device and a signal:

    Device dev = new Device("testdevice", 9000);
    Signal out_x = dev.add_output_signal("x", 1, 'f', null,
                                         new Double(0), new Double(1));

and make sure to poll the device during `draw()`:

    void draw() {
        ...
        dev.poll(0);
        ...
    }

Running this program should make a device called "testdevice" show up
in a libmapper GUI.  Use the steps in section "Testing", above, to
check that the device is created correctly.

We have noticed that Processing.org does not always free devices
correctly after closing the canvas window.  The Java test program uses
the following trick to ensure a finalizer is run when the JVM exits,
but it is not clear whether this is a good procedure for
Processing.org:

    // This is how to ensure the device is freed when the program
    // exits, even on SIGINT.  The Device must be declared "final".
    Runtime.getRuntime().addShutdownHook(new Thread()
        {
            @Override
            public void run()
                {
                    dev.free();
                }
        });

### Network interface issues

Since libmapper uses multicast instead of a central server, it must be
made possible for all computers on the network to see the same
multicast bus.  A requirement is that they are on the same subnet,
which usually means they are connected to the same router.  It is
possible to change the libmapper "TTL" settings, which expands the
reachability of the network to multiple subnet hops, but this is
considered an advanced usage scenario.

One problem that is often encountered especially by laptop users is
that multicast messages are sent to the wrong network interface.
Since the multicast IP address does not uniquely identify a single
network route, it is necessary for the software to specify the desired
NIC to use.  Selection of the NIC is supported by libmapper, but
programs may not always provide an interface for this.^[Such programs
are non-conforming!  All libmapper programs should display the current
network interface and allow selection of it from a list of the
computer's interfaces, either by name or by assigned IP address.]

Therefore one issue is especially prevalent, that computers may be
connected to the internet via wireless (wifi), and connected to a
router via a wired (ethernet) connection.  We recommend that laptop
users disable all but one interface while using libmapper.  If this is
unacceptable, the user is responsible for ensuring that libmapper
programs are provided the correct NIC information.

In the future a better approach may be needed.  Two ideas are:

  * Always send multicast messages on all NICs.
  * Provide a global configuration file for libmapper specifying which
    NIC to use.

Both of these solutions have their own complications, so if you have
an opinion on this topic or a better idea, please post to the
[libmapper mailing list][list].

[list]: http://groups.google.com/group/dot_mapper
