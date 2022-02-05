# Cross-compiling for Apple Silicon

So far I have only had success using clang rather than our usual gcc:

~~~
export CC=clang
export CXX=clang++
~~~

Start with a clean slate:

~~~
make clean
sudo make clean
~~~

Run `autogen.sh` with some extra flags:

~~~
./autogen.sh CFLAGS="-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk --target=arm64-apple-darwin" CXXFLAGS="-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk --target=arm64-apple-darwin" --build=x86_64-apple-darwin19.6.0 --host=arm64-apple-darwin
~~~

Note: we need to check/test the difference between using `--target=arm64-apple-darwin` and `--target=arm64-apple-macos11`. Both are recommended, is there a difference?

Next run `make` as usual.

~~~
make
~~~

This should build the individual object files from sources in src/, however the last step where the dylib is built will fail since the command doesn't currently include the `--target` argument. This needs to be fixed, but for now we will try just running the command manually:

~~~
cd src

clang -dynamiclib -Wl,-undefined -Wl,dynamic_lookup -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk --target=arm64-apple-darwin -o .libs/libmapper.9.dylib  .libs/libmapper_la-device.o .libs/libmapper_la-expression.o .libs/libmapper_la-graph.o .libs/libmapper_la-link.o .libs/libmapper_la-list.o .libs/libmapper_la-map.o .libs/libmapper_la-network.o .libs/libmapper_la-object.o .libs/libmapper_la-properties.o .libs/libmapper_la-router.o .libs/libmapper_la-signal.o .libs/libmapper_la-slot.o .libs/libmapper_la-table.o .libs/libmapper_la-time.o .libs/libmapper_la-value.o   -L/usr/local/lib /usr/local/lib/liblo.dylib -lz  -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk   -install_name  /usr/local/lib/libmapper.9.dylib -compatibility_version 10 -current_version 10.0 -Wl,-single_module
~~~

let's check the architecture of the dylib using `file`:

~~~
file .libs/libmapper.9.dylib 
~~~

we should get the response:

~~~
.libs/libmapper.9.dylib: Mach-O 64-bit dynamically linked shared library arm64
~~~

Once the product has been tested we should set up scripts to build "fat binaries" with i386, x86_64, and arm64 architectures.

----

building a "fat" binary:

~~~
cd /src/.libs
lipo -create -output libmapper.9.dylib libmapper.9.dylib.arm64 libmapper.9.dylib.x86_64
~~~
