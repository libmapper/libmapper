#!/bin/sh

# This script compiles universal binaries of the libmpr library
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

# Build darwin binaries for libmpr
# First argument must be the path to a libmpr source tarball.
LIBMPR_TAR="$1"
LIBLO_TAR="$2"

LIBMPR_VERSION=$(echo $LIBMPR_TAR|sed 's,.*libmpr-\(.*\).tar.gz,\1,')
LIBMPR_MAJOR=$(echo $LIBMPR_VERSION|sed 's,\([0-9]\)\(\.[0-9]*\)*,\1,')

LIBLO_VERSION=$(echo $LIBLO_TAR|sed 's,.*liblo-\(.*\).tar.gz,\1,')
LIBLO_MAJOR=$(echo $LIBLO_VERSION|sed 's,\([0-9]\)\(\.[0-9]*\)*,\1,')

if [ -z "$LIBMPR_TAR" ] || [ -z "$LIBLO_TAR" ]; then
    echo Usage: $0 '<libmpr-VERSION.tar.gz>' '<liblo-VERSION.tar.gz>'
    exit
fi

# Get absolute paths
LIBMPR_TAR="$PWD/$LIBMPR_TAR"
LIBLO_TAR="$PWD/$LIBLO_TAR"

mkdir -v binaries
cd binaries

function make_arches()
{
    tar -xzf "$LIBLO_TAR"

    cd $(basename "$LIBLO_TAR" .tar.gz)
    if ./configure CFLAGS="-arch i386 -arch x86_64" CXXFLAGS="-arch i386 -arch x86_64" LDFLAGS="-arch i386 -arch x86_64" --prefix=`pwd`/../install --enable-static --enable-shared --disable-dependency-tracking && make && make install; then
        cd ..
    else
        echo liblo Build error
        exit 1
    fi

    tar -xzf "$LIBMPR_TAR"

    cd $(basename "$LIBMPR_TAR" .tar.gz)
    PREFIX=`pwd`/../install
    if env PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig ./configure CFLAGS="-arch i386 -arch x86_64 -I$PREFIX/include" CXXFLAGS="-arch i386 -arch x86_64 -I$PREFIX/include" LDFLAGS="-arch i386 -arch x86_64 -L$PREFIX/lib  -Wl,-rpath,@loader_path/Frameworks -llo" --prefix=$PREFIX --enable-static --enable-shared --disable-dependency-tracking; then

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
            echo libmpr Build error
            exit 1
        fi
    else
        echo libmpr Build error
        exit 1
    fi

    LIBMPR_DYLIB=install/lib/libmpr.*.dylib

    install_name_tool \
        -id @rpath/mpr.framework/Versions/$LIBMPR_MAJOR/mpr \
        install/lib/libmpr.dylib || exit 1
    install_name_tool \
        -id @rpath/mpr.framework/Versions/$LIBMPR_MAJOR/mpr \
        $LIBMPR_DYLIB || exit 1
}

function rebuild_python_extentions()
{
    ARCH=$1
    cd $ARCH
    PREFIX=`pwd`/install

    cd libmpr-$LIBMPR_VERSION/swig
    make mpr_wrap.c

    gcc -DNDEBUG -g -fwrapv -Os -Wall -Wstrict-prototypes -arch $ARCH -pipe -I../src -I../include -I$PREFIX/include -I/System/Library/Frameworks/Python.framework/Versions/2.7/include/python2.7 -c mpr_wrap.c -o mpr_wrap.o

    gcc -Wl,-F. -bundle -undefined dynamic_lookup -arch $ARCH $SDKC $SDKLD mpr_wrap.o $PREFIX/lib/liblo.a $PREFIX/lib/libmpr.a -lpthread -o _mpr.so

    cd ../examples/py_tk_gui
    make pwm_wrap.cxx

    gcc -DNDEBUG -g -fwrapv -Os -Wall -Wstrict-prototypes -arch $ARCH -pipe -I../../src -I../../include -I../pwm_synth -I$PREFIX/include -I/System/Library/Frameworks/Python.framework/Versions/2.7/include/python2.7 -c pwm_wrap.cxx -o pwm_wrap.o

    gcc -Wl,-F. -bundle -undefined dynamic_lookup -arch $ARCH $SDKC $SDKLD pwm_wrap.o ../pwm_synth/.libs/libpwm.a -lpthread -o _pwm.so -framework CoreAudio -framework CoreFoundation

    cd ../../../..
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
	<string>libmpr.icns</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleShortVersionString</key>
	<string>$LIBMPR_VERSION</string>
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

    APP=bundles/libmpr_PWM_Example.app
    mkdir -v $APP
    mkdir -v $APP/Contents
    mkdir -v $APP/Contents/MacOS
    mkdir -v $APP/Contents/Resources
    cp -v libmpr-$LIBMPR_VERSION/swig/_mpr.so $APP/Contents/MacOS/
    cp -v libmpr-$LIBMPR_VERSION/swig/mpr.py $APP/Contents/MacOS/
    cp -v libmpr-$LIBMPR_VERSION/examples/py_tk_gui/_pwm.so $APP/Contents/MacOS/
    cp -v libmpr-$LIBMPR_VERSION/examples/py_tk_gui/pwm.py $APP/Contents/MacOS/
    cp -v libmpr-$LIBMPR_VERSION/examples/py_tk_gui/tk_pwm.py $APP/Contents/MacOS/
    echo 'APPL????' >$APP/Contents/PkgInfo
    info_plist $APP/Contents/Info.plist libmpr_PWM_Example tk_pwm.py
    cp -v ../icons/libmpr.icns $APP/Contents/Resources/

    APP=bundles/libmpr_Slider_Example.app
    mkdir -v $APP
    mkdir -v $APP/Contents
    mkdir -v $APP/Contents/MacOS
    mkdir -v $APP/Contents/Resources
    cp -v libmpr-$LIBMPR_VERSION/swig/_mpr.so $APP/Contents/MacOS/
    cp -v libmpr-$LIBMPR_VERSION/swig/mpr.py $APP/Contents/MacOS/
    cp -v libmpr-$LIBMPR_VERSION/swig/tkgui.py $APP/Contents/MacOS/
    echo 'APPL????' >$APP/Contents/PkgInfo
    info_plist $APP/Contents/Info.plist libmpr_Slider_Example tkgui.py
    cp -v ../icons/libmpr.icns $APP/Contents/Resources/

    APP=bundles/libmpr_Slider_Launcher.app
    mkdir -v $APP
    mkdir -v $APP/Contents
    mkdir -v $APP/Contents/MacOS
    mkdir -v $APP/Contents/Resources
    cp -v libmpr-$LIBMPR_VERSION/extra/osx/libmpr_slider_launcher.py $APP/Contents/MacOS/
    echo 'APPL????' >$APP/Contents/PkgInfo
    info_plist $APP/Contents/Info.plist libmpr_Slider_Launcher libmpr_slider_launcher.py
    cp -v ../icons/libmpr.icns $APP/Contents/Resources/

    FRAMEWORK=bundles/mpr.framework
    mkdir -v $FRAMEWORK
    mkdir -v $FRAMEWORK/Versions
    mkdir -v $FRAMEWORK/Versions/$LIBMPR_MAJOR
    mkdir -v $FRAMEWORK/Versions/$LIBMPR_MAJOR/Libraries
    cp -rv install/lib/libmpr.*.dylib \
        $FRAMEWORK/Versions/$LIBMPR_MAJOR/Libraries/
    find $FRAMEWORK/Versions/$LIBMPR_MAJOR/Libraries -type f \
        -exec chmod 755 {} \;
    ln -sv $FRAMEWORK/Versions/$LIBMPR_MAJOR/Libraries/libmpr.*.dylib \
        $FRAMEWORK/Versions/$LIBMPR_MAJOR/Libraries/libmpr.dylib
    mkdir -v $FRAMEWORK/Versions/$LIBMPR_MAJOR/Headers
    cp -rv install/include/mpr/* \
        $FRAMEWORK/Versions/$LIBMPR_MAJOR/Headers/
    find $FRAMEWORK/Versions/$LIBMPR_MAJOR/Headers -type f \
        -exec chmod 644 {} \;
    ln -sv $LIBMPR_MAJOR $FRAMEWORK/Versions/Current
    ln -sv Versions/Current/Headers $FRAMEWORK/Headers
    mkdir -v $FRAMEWORK/Versions/$LIBMPR_MAJOR/Resources
    ln -sv Versions/Current/Resources $FRAMEWORK/Resources

    cat >$FRAMEWORK/Versions/$LIBMPR_MAJOR/Resources/Info.plist <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>English</string>
    <key>CFBundleExecutable</key>
    <string>mpr</string>
    <key>CFBundleIdentifier</key>
    <string>org.idmil.mpr</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>mpr</string>
    <key>CFBundlePackageType</key>
    <string>FMWK</string>
    <key>CFBundleSignature</key>
    <string>????</string>
    <key>CFBundleVersion</key>
    <string>1.1</string>
</dict>
</plist>
EOF

    # Subframework for liblo
    mkdir $FRAMEWORK/Versions/$LIBMPR_MAJOR/Frameworks
    ln -sv Versions/$LIBMPR_MAJOR/Frameworks $FRAMEWORK/Frameworks
    FRAMEWORK=$FRAMEWORK/Versions/$LIBMPR_MAJOR/Frameworks/lo.framework
    mkdir -v $FRAMEWORK
    mkdir -v $FRAMEWORK/Versions
    mkdir -v $FRAMEWORK/Versions/$LIBLO_MAJOR
    mkdir -v $FRAMEWORK/Versions/$LIBLO_MAJOR/Libraries
    cp -rv install/lib/liblo.*.dylib $FRAMEWORK/Versions/$LIBLO_MAJOR/Libraries/
    ln -sv $FRAMEWORK/Versions/$LIBLO_MAJOR/Libraries/liblo.*.dylib \
        $FRAMEWORK/Versions/$LIBLO_MAJOR/Libraries/liblo.dylib
    find $FRAMEWORK/Versions/$LIBLO_MAJOR/Libraries -type f -exec chmod 755 {} \;
    mkdir -v $FRAMEWORK/Versions/$LIBLO_MAJOR/Headers
    cp -rv install/include/lo/* \
        $FRAMEWORK/Versions/$LIBLO_MAJOR/Headers/
    find $FRAMEWORK/Versions/$LIBLO_MAJOR/Headers -type f -exec chmod 644 {} \;
    ln -sv $LIBLO_MAJOR $FRAMEWORK/Versions/Current
    ln -sv Versions/Current/Headers $FRAMEWORK/Headers
    mkdir -v $FRAMEWORK/Versions/$LIBLO_MAJOR/Resources
    ln -sv Versions/Current/Resources $FRAMEWORK/Resources

    cat >$FRAMEWORK/Resources/Info.plist <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>English</string>
    <key>CFBundleExecutable</key>
    <string>lo</string>
    <key>CFBundleIdentifier</key>
    <string>org.idmil.lo</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>lo</string>
    <key>CFBundlePackageType</key>
    <string>FMWK</string>
    <key>CFBundleSignature</key>
    <string>????</string>
    <key>CFBundleVersion</key>
    <string>0.29</string>
</dict>
</plist>
EOF
}

make_arches

make_bundles

echo Done.
