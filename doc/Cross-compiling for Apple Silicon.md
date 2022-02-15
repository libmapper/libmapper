# Cross-compiling liblo and libmapper for Apple Silicon

So far I have only had success using clang rather than our usual gcc:

~~~ bash
export CC=clang
export CXX=clang++
~~~

## liblo

Start with a clean slate:

~~~ bash
make clean
sudo make clean
~~~

Run `autogen.sh` with some extra flags:

~~~ bash
./autogen.sh CFLAGS="-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk --target=arm64-apple-darwin" CXXFLAGS="-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk --target=arm64-apple-darwin" --build=x86_64-apple-darwin19.6.0 --host=aarch64-apple-darwin
~~~

Next run `make` as usual.

~~~ bash
make
~~~

This should build the individual object files from sources in src/, however the last step where the dylib is built will fail since the command doesn't currently include the `--target` argument. This needs to be fixed, but for now we will try just running the command manually:

~~~ bash
cd src
clang -dynamiclib -Wl,-undefined -Wl,dynamic_lookup -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk --target=arm64-apple-darwin -o .libs/liblo.7.dylib  .libs/liblo_la-address.o .libs/liblo_la-send.o .libs/liblo_la-message.o .libs/liblo_la-server.o .libs/liblo_la-method.o .libs/liblo_la-blob.o .libs/liblo_la-bundle.o .libs/liblo_la-timetag.o .libs/liblo_la-pattern_match.o .libs/liblo_la-version.o .libs/liblo_la-server_thread.o    -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk -O0 -g   -install_name  /usr/local/lib/liblo.7.dylib -compatibility_version 12 -current_version 12.1 -Wl,-single_module
cd ..
~~~

let's check the architecture of the dylib using `file`:

~~~ bash
file src/.libs/liblo.7.dylib 
~~~

we should get the response:

~~~ bash
src/.libs/liblo.7.dylib: Mach-O 64-bit dynamically linked shared library arm64
~~~

if you chose to build a static version of the library you can check the architecture using:

~~~ bash
lipo -info /src/.libs/liblo.a
~~~

## libmapper

Start with a clean slate:

~~~ bash
make clean
sudo make clean
~~~

Run `autogen.sh` with some extra flags:

~~~ bash
./autogen.sh CFLAGS="-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk --target=arm64-apple-darwin" CXXFLAGS="-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk --target=arm64-apple-darwin" --build=x86_64-apple-darwin19.6.0 --host=aarch64-apple-darwin
~~~

Next run `make` as usual.

~~~ bash
make
~~~

This should build the individual object files from sources in src/, however the last step where the dylib is built will fail since the command doesn't currently include the `--target` argument. This needs to be fixed, but for now we will try just running the command manually:

~~~ bash
cd src
clang -dynamiclib -Wl,-undefined -Wl,dynamic_lookup -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk --target=arm64-apple-darwin -o .libs/libmapper.9.dylib .libs/libmapper_la-device.o .libs/libmapper_la-expression.o .libs/libmapper_la-graph.o .libs/libmapper_la-link.o .libs/libmapper_la-list.o .libs/libmapper_la-map.o .libs/libmapper_la-network.o .libs/libmapper_la-object.o .libs/libmapper_la-properties.o .libs/libmapper_la-router.o .libs/libmapper_la-signal.o .libs/libmapper_la-slot.o .libs/libmapper_la-table.o .libs/libmapper_la-time.o .libs/libmapper_la-value.o   -L/usr/local/lib /usr/local/lib/liblo.dylib -lz  -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk -install_name /usr/local/lib/libmapper.9.dylib -compatibility_version 10 -current_version 10.0 -Wl,-single_module
~~~

let's check the architecture of the dylib using `file`:

~~~ bash
file src/.libs/libmapper.9.dylib 
~~~

we should get the response:

~~~ bash
src/.libs/libmapper.9.dylib: Mach-O 64-bit dynamically linked shared library arm64
~~~

if you chose to build a static version of the library you can check the architecture using:

~~~ bash
lipo -info /src/.libs/libmapper.a
~~~

Once the product has been tested we should set up scripts to build "fat binaries" with i386, x86_64, and arm64 architectures.

----

## building a "fat" binary

For each of [liblo, libmapper] build both the arm64 and x86_64 versions of the library and rename them, e.g.

~~~ bash
# follow directions to make install arm64 version then:
mv /usr/local/lib/liblo.7.dylib /usr/local/lib/liblo.7.dylib.arm64
# make install x86_64 version as normal, then:
mv /usr/local/lib/liblo.7.dylib /usr/local/lib/liblo.7.x86_64
lipo -create -output /usr/local/lib/liblo.7.dylib /usr/local/lib/liblo.7.dylib.arm64 /usr/local/lib/liblo.7.dylib.x86_64
~~~
