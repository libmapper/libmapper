Building libmapper
==================

This file documents the build process for libmapper for various
operating systems, and will be updated as the libmapper core project
progresses. Supports Linux, MacOS and Windows platforms.

Linux and MacOS
--------------

### Dependencies

libmapper depends on version 0.30 of liblo or later.
Please consult the [LibLo project page][liblo] for details.

[liblo]: http://liblo.sourceforge.net

The GNU configure step detects liblo using the "pkg-config" program.
This is usually already installed in Linux systems, but on MacOS, we
recommend installing it via [MacPorts][ports] or [HomeBrew][brew]. In
the future libmapper packages for MacPorts and HomeBrew will be
directly provided, but this is not yet the case.

[ports]: http://www.macports.org
[brew]: http://mxcl.github.com/homebrew

You may wish to manually check that the correct version of liblo is
detected, by running,

    pkg-config --libs --cflags liblo

If the path to liblo is not correct, please set up the
`PKG_CONFIG_PATH` environment variable appropriately and try again.

libmapper also has optional dependencies on the Java SDK and Doxygen,
for Java bindings, and documentation, respectively.  The Java SDK may
be installed according to your standard operating system procedure,
and you can check if it is already installed by running,

    javac -version

These should print the installed version numbers of these programs if
correctly configured.  You may install the `doxygen` package
using your preferred package manager.

The `examples` folder also contains an audio example, which binds to
`libasound` on Linux, and CoreAudio on MacOS.  On Ubuntu, you may
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
home directory such as `$HOME/.local`.  Users of HomeBrew on MacOS
should always use `/usr/local`.

If you will be hacking on libmapper or wish to have verbose output
while it is running, we recommended enabling debug mode:

    ./configure --enable-debug

Additionally, Java and Python bindings, and audio examples, may be
disabled with options `--disable-jni`, `--disable-python`, and
`--disable-audio` respectively.

After `configure` runs successfully, the configuration options will be
printed for your confirmation.  If anything unexpected occurs, be sure
to check `config.log` for information about what failed.

### Building

Once the build is configured, build it with,

    make

Should any errors occur, please inform the [libmapper mailing list][list].

To verify that the library runs without errors, you may run the main
test program, available in the test directory:

    ./test/test -t

This program creates devices and signals and tests sending data between them using maps. The `-t` flag will cause the test program to automatically terminate after sending 200 messages, so omit it if you want to keep the test running indefinately.

Note that the programs in the test directory are _not_ examples of
library usage, as they additionally test some internal functions in
the library.  Please see the `examples` directory for examples of how
to use the library.

### Installing

The software may be installed with,

    sudo make install

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

[webmapper]: https://github.com/libmapper/webmapper

You should also test the Python and Java bindings if you plan to use
them.  Some other programs may depend on them – for example webmapper uses the Python bindings.

To test that the Python module is working, it is generally enough to
run the following command,

    python -m mapper

This will import the `mapper` module, which will fail if either
`mapper.py` or the native portion of the binding are not found.  You
may need to adjust your `PYTHONPATH` variable to ensure these can be
found.

You can test running programs by `cd`'ing to the `bindings/python`
folder and running,

    cd bindings/python
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

Building on Windows
-------------------

### Dependencies

libmapper depends on version 0.30 of liblo or later.
Please clone the [LibLo repository][liblo] and consult its documentation to build for Windows.

[liblo]: https://github.com/radarsat1/liblo

Cmake is also required to generate visual studio solutions, and can be installed [here][cmake]. Add it to the environment path when prompted for terminal access later on.

[cmake]: https://cmake.org/download/

Zlib is required as well, which you can pick up [from nuget][zlib]. You can use the Visual Studio Tools->NuGet Package Manager Console to install it easily.

[zlib]: https://www.nuget.org/packages/zlib-msvc14-x64/

Finally, you'll need Visual Studio 2017 or 2019, which you can grab [here][visual_studio]. Be sure to install the C++ developer tools when installing if you don't already have them.

[visual_studio]: https://visualstudio.microsoft.com/vs/

### Configuring

Once libmapper is downloaded, open a terminal in its root folder.

Create a build directory and cd into it

    mkdir ./build
    cd build

Modify the CMakeLists.txt file in the root folder, replacing the paths near the top with your local paths.

Open <your-zlib-root>/build/native/include/zconf.h and search for the line:

    #ifndef Z_SOLO

which should be around line 476. Add the following line above it and save to avoid a common build error:

    #undef Z_HAVE_UNISTD_H

Run the following to generate a solution, replacing your version's details:

    cmake -G "Visual Studio 16 2019" ..

### Building

By now, you should have a Visual Studio solution in the ./build directory. Open the .sln and build the libmapper project.

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

We have found that in some cases, especially on MacOS, there are
programs that do not use the computer's native architecture.  For
example, Processing.org and Cycling 74's Max/MSP prior to v8 are 32-bit
applications, even if you are running a 64-bit version of MacOS.
Therefore after building libmapper, it _will not work_ with these
programs.

We recommend that on MacOS you build a universal binary, since we have
found it saves a lot of trouble later on.  Note that you must build
universal binaries of liblo as well as libmapper.

To do so, for both liblo and libmapper, you must perform the
`configure` and `make` steps with the following flags:

    ./configure CFLAGS="-arch i386 -arch x86_64" \
                CXXFLAGS="-arch i386 -arch x86_64" \
                --disable-dependency-tracking
    make

The `file` command should list both 32- and 64-bit architectures.

    file src/.libs/libmapper.8.dylib

(Of course, replace ".8" with the current libmapper version.)

Although we do not explicitly list them here, similar steps should be
performed for liblo.

### Processing.org

Processing.org is a Java-based IDE and set of libraries for developing
visualizations and interactive art.  To use libmapper with Processing,
you should place the JNI bindings into the appropriate locations.

From the libmapper directory, create a directory called
`libraries/libmapper/library` under your sketchbook directory:

    mkdir -p <sketchbook>/libraries/libmapper/library

Now copy the JNI bindings to this directory, renaming the jar file to
match the name of the directory:

    cp jni/.libs/libmapperjni.8.dylib <sketchbook>/libraries/libmapper/library/libmapperjni.dylib
    cp jni/libmapper.jar <sketchbook>/libraries/libmapper/library/libmapper.jar

Create a file `export.txt`:

    echo 'name = libmapper' > "<sketchbook>/libraries/libmapper/library/export.txt"

Now, when you run the Processing IDE, you should see "libmapper"
listed under "Sketch/Import Library...".

Choosing this will insert two lines at the top of your sketch:

    import mapper.*;
    import mapper.graph.*;

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
