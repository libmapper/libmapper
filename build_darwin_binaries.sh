#!/bin/sh

# This script compiles universal binaries of the libmapper library
# and builds app bundles for the examples and a framework.

ARCHES="i386 x86_64"
SDK="-iwithsysroot=/Developer/SDKs/MacOSX10.4u.sdk/"

# Build darwin binaries for libmapper
# First argument must be the path to a libmapper source tarball.
LIBMAPPER_TAR="$1"
LIBLO_TAR="$2"

LIBMAPPER_VERSION=$(echo $LIBMAPPER_TAR|sed 's,.*libmapper-\(.*\).tar.gz,\1,')
LIBMAPPER_MAJOR=$(echo $LIBMAPPER_VERSION|sed 's,\([0-9]\)\(\.[0-9]\)*,\1,')

if [ -z "$LIBMAPPER_TAR" ] || [ -z "$LIBLO_TAR" ]; then
    echo Usage: $0 '<libmapper-VERSION.tar.gz>' '<liblo-VERSION.tar.gz>'
    exit
fi

# Get absolute paths
LIBMAPPER_TAR="$PWD/$LIBMAPPER_TAR"
LIBLO_TAR="$PWD/$LIBLO_TAR"

mkdir -v binaries
cd binaries

function make_arch()
{
    ARCH=$1

    mkdir -v $ARCH
    cd $ARCH

    tar -xzf "$LIBLO_TAR"

    cd $(basename "$LIBLO_TAR" .tar.gz)
    if ./configure CFLAGS="-arch $ARCH $SDK" CXXFLAGS="-arch $ARCH $SDK" LDFLAGS="-arch $ARCH $SDK" --prefix=`pwd`/../install --enable-static --enable-dynamic && make && make install; then
        cd ..
    else
        echo Build error in arch $ARCH
        exit 1
    fi

    tar -xzf "$LIBMAPPER_TAR"

    cd $(basename "$LIBMAPPER_TAR" .tar.gz)
    PREFIX=`pwd`/../install
    if ./configure CFLAGS="-arch $ARCH $SDK -I$PREFIX/include" CXXFLAGS="-arch $ARCH $SDK -I$PREFIX/include" LDFLAGS="-arch $ARCH $SDK -L$PREFIX/lib" --prefix=$PREFIX --enable-static --enable-dynamic && make && make install; then
        cd ..
    else
        echo Build error in arch $ARCH
        exit 1
    fi

    cd ..
}

function rebuild_python_extentions()
{
    ARCH=$1
    cd $ARCH
    PREFIX=`pwd`/install

    cd libmapper-$LIBMAPPER_VERSION/swig
    make mapper_wrap.c

    gcc-4.2 -DNDEBUG -g -fwrapv -Os -Wall -Wstrict-prototypes -arch $ARCH -pipe -I../src -I../include -I$PREFIX/include -I/System/Library/Frameworks/Python.framework/Versions/2.6/include/python2.6 -c mapper_wrap.c -o mapper_wrap.o

    gcc-4.2 -Wl,-F. -bundle -undefined dynamic_lookup -arch $ARCH $SDK mapper_wrap.o $PREFIX/lib/liblo.a $PREFIX/lib/libmapper-0.a -lpthread -o _mapper.so

    cd ../examples/py_tk_gui
    make pwm_wrap.c

    gcc-4.2 -DNDEBUG -g -fwrapv -Os -Wall -Wstrict-prototypes -arch $ARCH -pipe -I../../src -I../../include -I../pwm_synth -I$PREFIX/include -I/System/Library/Frameworks/Python.framework/Versions/2.6/include/python2.6 -c pwm_wrap.cxx -o pwm_wrap.o

    gcc-4.2 -Wl,-F. -bundle -undefined dynamic_lookup -arch $ARCH $SDK pwm_wrap.o ../pwm_synth/.libs/libpwm.a $PREFIX/lib/liblo.a $PREFIX/lib/libmapper-0.a -lpthread -o _pwm.so -framework CoreAudio -framework CoreFoundation

    cd ../../../..
}

function use_lipo()
{
    mkdir -v all
    mkdir -v all/lib
    for i in i386/install/lib/*.{dylib,a}; do
        ARCHFILES=""
        for a in $ARCHES; do
            ARCHFILES="$ARCHFILES -arch $a $a/install/lib/$(basename $i)"
        done
        lipo -create -output all/lib/$(basename $i) $ARCHFILES
    done

    mkdir -v all/python
    for i in $(find i386 -name _*.so); do
        ARCHFILES=""
        for a in $ARCHES; do
            ARCHFILES="$ARCHFILES -arch $a $(echo $i | sed s/i386/$a/)"
        done
        lipo -create -output all/python/$(basename $i) $ARCHFILES
    done
}

function info_plist()
{
    FILENAME=$1
    NAME=$2
    EXECNAME=$3
    cat >$FILENAME <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>English</string>
	<key>CFBundleDisplayName</key>
	<string>$NAME</string>
	<key>CFBundleExecutable</key>
	<string>$EXECNAME</string>
	<key>CFBundleIdentifier</key>
	<string>org.idmil.$NAME</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleName</key>
	<string>$NAME</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleShortVersionString</key>
	<string>$LIBMAPPER_VERSION</string>
	<key>CFBundleVersion</key>
	<string>273</string>
	<key>LSMinimumSystemVersion</key>
	<string>10.5</string>
</dict>
</plist>
EOF
}

function make_bundles()
{
    mkdir -v bundles

    APP=bundles/libmapper_PWM_Example.app
    mkdir -v $APP
    mkdir -v $APP/Contents
    mkdir -v $APP/Contents/MacOS
    cp -v all/python/_mapper.so $APP/Contents/MacOS/
    cp -v all/python/_pwm.so $APP/Contents/MacOS/
    cp -v i386/libmapper-$LIBMAPPER_VERSION/swig/mapper.py $APP/Contents/MacOS/
    cp -v i386/libmapper-$LIBMAPPER_VERSION/examples/py_tk_gui/pwm.py $APP/Contents/MacOS/
    cp -v i386/libmapper-$LIBMAPPER_VERSION/examples/py_tk_gui/tk_pwm.py $APP/Contents/MacOS/
    echo 'APPL????' >$APP/Contents/PkgInfo
    info_plist $APP/Contents/Info.plist libmapper_PWM_Example tk_pwm.py

    APP=bundles/libmapper_Slider_Example.app
    mkdir -v $APP
    mkdir -v $APP/Contents
    mkdir -v $APP/Contents/MacOS
    cp -v all/python/_mapper.so $APP/Contents/MacOS/
    cp -v i386/libmapper-$LIBMAPPER_VERSION/swig/mapper.py $APP/Contents/MacOS/
    cp -v i386/libmapper-$LIBMAPPER_VERSION/swig/tkgui.py $APP/Contents/MacOS/
    echo 'APPL????' >$APP/Contents/PkgInfo
    info_plist $APP/Contents/Info.plist libmapper_Slider_Example tkgui.py

    FRAMEWORK=bundles/libmapper.framework
    mkdir -v $FRAMEWORK
    mkdir -v $FRAMEWORK/Contents
    mkdir -v $FRAMEWORK/Versions
    mkdir -v $FRAMEWORK/Versions/$LIBMAPPER_MAJOR
    cp -v all/lib/libmapper-$LIBMAPPER_VERSION.dylib \
        $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/libmapper
    chmod 664 $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/libmapper
    mkdir -v $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/Headers
    cp -rv i386/install/include/mapper-0/* \
        $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/Headers/
    find $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/Headers -type f \
        -exec chmod 664 {} \;
    ln -s Versions/$LIBMAPPER_MAJOR/libmapper $FRAMEWORK/libmapper
    ln -s Versions/$LIBMAPPER_MAJOR/Headers $FRAMEWORK/Headers
    ln -s $LIBMAPPER_MAJOR $FRAMEWORK/Versions/Current

    cat >$FRAMEWORK/Contents/Info.plist <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist SYSTEM "file://localhost/System/Library/DTDs/PropertyList.dtd">
<plist version="0.9">
<dict>
	<key>CFBundlePackageType</key>
	<string>FMWK</string>
        <key>CFBundleShortVersionString</key>
        <string>4.7</string>
        <key>CFBundleGetInfoString</key>
	<key>CFBundleSignature</key>
	<string>????</string>
	<key>CFBundleExecutable</key>
	<string>libmapper</string>
</dict>
</plist>
EOF
}

for i in $ARCHES; do make_arch $i; done

for i in $ARCHES; do rebuild_python_extentions $i; done

use_lipo

make_bundles

echo Done.
