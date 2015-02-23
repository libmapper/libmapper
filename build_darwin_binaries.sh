#!/bin/sh

# This script compiles universal binaries of the libmapper library
# and builds app bundles for the examples and a framework.

ARCHES="i386 x86_64"

if [ -d /Developer/SDKs/MacOSX10.5.sdk ]; then
    SDKPATH="/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs"
else
    SDKPATH=/Developer/SDKs/
fi
if [ -d "$SDKPATH" ]; then
    for i in 5 6 7 8; do
        if [ -d "$SDKPATH/MacOSX10.$i.sdk" ]; then
            SDK="$SDKPATH/MacOSX10.$i.sdk"
            break
        fi
    done
fi

SDKC="--sysroot=$SDK"
SDKLD="-lgcc_s.1"

# Build darwin binaries for libmapper
# First argument must be the path to a libmapper source tarball.
LIBMAPPER_TAR="$1"
LIBLO_TAR="$2"

LIBMAPPER_VERSION=$(echo $LIBMAPPER_TAR|sed 's,.*libmapper-\(.*\).tar.gz,\1,')
LIBMAPPER_MAJOR=$(echo $LIBMAPPER_VERSION|sed 's,\([0-9]\)\(\.[0-9]*\)*.*,\1,')

LIBLO_VERSION=$(echo $LIBLO_TAR|sed 's,.*liblo-\(.*\).tar.gz,\1,')
LIBLO_MAJOR=$(echo $LIBLO_VERSION|sed 's,\([0-9]\)\(\.[0-9]*\)*,\1,')

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
    if ./configure CFLAGS="-arch $ARCH $SDKC" CXXFLAGS="-arch $ARCH $SDKC $SDKCXX" LDFLAGS="-arch $ARCH $SDKC $SDKLD" --prefix=`pwd`/../install --enable-static --enable-shared && make && make install; then
        cd ..
    else
        echo Build error in arch $ARCH
        exit 1
    fi

    tar -xzf "$LIBMAPPER_TAR"

    cd $(basename "$LIBMAPPER_TAR" .tar.gz)
    PREFIX=`pwd`/../install
    if env PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig ./configure --enable-debug CFLAGS="-arch $ARCH $SDKC -I$PREFIX/include" CXXFLAGS="-arch $ARCH $SDKC $SDKCXX -I$PREFIX/include" LDFLAGS="-arch $ARCH $SDKC $SDKLD -L$PREFIX/lib  -Wl,-rpath,@loader_path/Frameworks -llo" --prefix=$PREFIX --enable-static --enable-shared; then

        LIBLO_DYLIB=$(ls ../install/lib/liblo.*.dylib)

        install_name_tool \
            -id @rpath/lo.framework/Versions/$LIBLO_MAJOR/lo \
            ../install/lib/liblo.dylib || exit 1
        install_name_tool \
            -id @rpath/lo.framework/Versions/$LIBLO_MAJOR/lo \
            $LIBLO_DYLIB || exit 1

        if make && make install; then
            cd ..
        else
            echo Build error in arch $ARCH
            exit 1
        fi
    else
        echo Build error in arch $ARCH
        exit 1
    fi

    LIBMAPPER_DYLIB=install/lib/libmapper-*.*.dylib

    install_name_tool \
        -id @rpath/mapper.framework/Versions/$LIBMAPPER_MAJOR/mapper \
        install/lib/libmapper-$LIBMAPPER_MAJOR.dylib || exit 1
    install_name_tool \
        -id @rpath/mapper.framework/Versions/$LIBMAPPER_MAJOR/mapper \
        $LIBMAPPER_DYLIB || exit 1

    cd ..
}

function rebuild_python_extentions()
{
    ARCH=$1
    cd $ARCH
    PREFIX=`pwd`/install

    cd libmapper-$LIBMAPPER_VERSION/swig
    make mapper_wrap.c

    gcc -DNDEBUG -g -fwrapv -Os -Wall -Wstrict-prototypes -arch $ARCH -pipe -I../src -I../include -I$PREFIX/include -I/System/Library/Frameworks/Python.framework/Versions/2.6/include/python2.6 -c mapper_wrap.c -o mapper_wrap.o

    gcc -Wl,-F. -bundle -undefined dynamic_lookup -arch $ARCH $SDKC $SDKLD mapper_wrap.o $PREFIX/lib/liblo.a $PREFIX/lib/libmapper-0.a -lpthread -o _mapper.so

    cd ../examples/py_tk_gui
    make pwm_wrap.cxx

    gcc -DNDEBUG -g -fwrapv -Os -Wall -Wstrict-prototypes -arch $ARCH -pipe -I../../src -I../../include -I../pwm_synth -I$PREFIX/include -I/System/Library/Frameworks/Python.framework/Versions/2.6/include/python2.6 -c pwm_wrap.cxx -o pwm_wrap.o

    gcc -Wl,-F. -bundle -undefined dynamic_lookup -arch $ARCH $SDKC $SDKLD pwm_wrap.o ../pwm_synth/.libs/libpwm.a -lpthread -o _pwm.so -framework CoreAudio -framework CoreFoundation

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
        lipo -create -output all/lib/$(basename $i) $ARCHFILES || exit 1
    done

    mkdir -v all/python
    for i in libmapper-$LIBMAPPER_VERSION/examples/py_tk_gui/_pwm.so libmapper-$LIBMAPPER_VERSION/swig/_mapper.so; do
        ARCHFILES=""
        for a in $ARCHES; do
            ARCHFILES="$ARCHFILES -arch $a $a/$i"
        done
        lipo -create -output all/python/$(basename $i) $ARCHFILES || exit 1
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
	<key>CFBundleIconFile</key>
	<string>libmapper_doc.icns</string>
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
    mkdir -v $APP/Contents/Resources
    cp -v all/python/_mapper.so $APP/Contents/MacOS/
    cp -v all/python/_pwm.so $APP/Contents/MacOS/
    cp -v i386/libmapper-$LIBMAPPER_VERSION/swig/mapper.py $APP/Contents/MacOS/
    cp -v i386/libmapper-$LIBMAPPER_VERSION/examples/py_tk_gui/pwm.py $APP/Contents/MacOS/
    cp -v i386/libmapper-$LIBMAPPER_VERSION/examples/py_tk_gui/tk_pwm.py $APP/Contents/MacOS/
    echo 'APPL????' >$APP/Contents/PkgInfo
    info_plist $APP/Contents/Info.plist libmapper_PWM_Example tk_pwm.py
    cp -v ../icons/libmapper_doc.icns $APP/Contents/Resources/

    APP=bundles/libmapper_Slider_Example.app
    mkdir -v $APP
    mkdir -v $APP/Contents
    mkdir -v $APP/Contents/MacOS
    mkdir -v $APP/Contents/Resources
    cp -v all/python/_mapper.so $APP/Contents/MacOS/
    cp -v i386/libmapper-$LIBMAPPER_VERSION/swig/mapper.py $APP/Contents/MacOS/
    cp -v i386/libmapper-$LIBMAPPER_VERSION/swig/tkgui.py $APP/Contents/MacOS/
    echo 'APPL????' >$APP/Contents/PkgInfo
    info_plist $APP/Contents/Info.plist libmapper_Slider_Example tkgui.py
    cp -v ../icons/libmapper_doc.icns $APP/Contents/Resources/

    APP=bundles/libmapper_Slider_Launcher.app
    mkdir -v $APP
    mkdir -v $APP/Contents
    mkdir -v $APP/Contents/MacOS
    mkdir -v $APP/Contents/Resources
    cp -v i386/libmapper-$LIBMAPPER_VERSION/extra/osx/libmapper_slider_launcher.py $APP/Contents/MacOS/
    echo 'APPL????' >$APP/Contents/PkgInfo
    info_plist $APP/Contents/Info.plist libmapper_Slider_Launcher libmapper_slider_launcher.py
    cp -v ../icons/libmapper_doc.icns $APP/Contents/Resources/

    FRAMEWORK=bundles/mapper.framework
    mkdir -v $FRAMEWORK
    mkdir -v $FRAMEWORK/Contents
    mkdir -v $FRAMEWORK/Versions
    mkdir -v $FRAMEWORK/Versions/$LIBMAPPER_MAJOR
    cp -v all/lib/libmapper-$LIBMAPPER_MAJOR.dylib \
        $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/mapper
    chmod 664 $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/mapper
    mkdir -v $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/Headers
    cp -rv i386/install/include/mapper-$LIBMAPPER_MAJOR/mapper/* \
        $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/Headers/
    find $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/Headers -type f \
        -exec chmod 664 {} \;
    ln -s Versions/$LIBMAPPER_MAJOR/mapper $FRAMEWORK/mapper
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
	<string>mapper</string>
</dict>
</plist>
EOF

    # Subframework for liblo
    mkdir $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/Frameworks
    ln -s Versions/$LIBMAPPER_MAJOR/Frameworks $FRAMEWORK/Frameworks
    FRAMEWORK=$FRAMEWORK/Versions/0/Frameworks/lo.framework
    mkdir -v $FRAMEWORK
    mkdir -v $FRAMEWORK/Contents
    mkdir -v $FRAMEWORK/Versions
    mkdir -v $FRAMEWORK/Versions/$LIBLO_MAJOR
    cp -v all/lib/liblo.dylib $FRAMEWORK/Versions/$LIBLO_MAJOR/lo
    chmod 664 $FRAMEWORK/Versions/$LIBLO_MAJOR/lo
    mkdir -v $FRAMEWORK/Versions/$LIBLO_MAJOR/Headers
    cp -rv i386/install/include/lo/* \
        $FRAMEWORK/Versions/$LIBLO_MAJOR/Headers/
    find $FRAMEWORK/Versions/$LIBLO_MAJOR/Headers -type f -exec chmod 664 {} \;
    ln -s Versions/$LIBLO_MAJOR/lo $FRAMEWORK/lo
    ln -s Versions/$LIBLO_MAJOR/Headers $FRAMEWORK/Headers
    ln -s $LIBLO_MAJOR $FRAMEWORK/Versions/Current

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
	<string>lo</string>
</dict>
</plist>
EOF
}

for i in $ARCHES; do make_arch $i; done

for i in $ARCHES; do rebuild_python_extentions $i; done

use_lipo

make_bundles

echo Done.
