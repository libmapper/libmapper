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
LIBMAPPER_MAJOR=$(echo $LIBMAPPER_VERSION|sed 's,\([0-9]\)\(\.[0-9]*\)*,\1,')

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

    tar -xzf "$LIBMAPPER_TAR"

    cd $(basename "$LIBMAPPER_TAR" .tar.gz)
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
            echo libmapper Build error
            exit 1
        fi
    else
        echo libmapper Build error
        exit 1
    fi

    LIBMAPPER_DYLIB=install/lib/libmapper.*.dylib

    install_name_tool \
        -id @rpath/mapper.framework/Versions/$LIBMAPPER_MAJOR/mapper \
        install/lib/libmapper.dylib || exit 1
    install_name_tool \
        -id @rpath/mapper.framework/Versions/$LIBMAPPER_MAJOR/mapper \
        $LIBMAPPER_DYLIB || exit 1
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
	<string>libmapper.icns</string>
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
    cp -v libmapper-$LIBMAPPER_VERSION/src/.libs/libmapper.*.dylib $APP/Contents/MacOS/libmapper.dylib
    cp -v libmapper-$LIBMAPPER_VERSION/examples/pwm_example $APP/Contents/MacOS/
    echo 'APPL????' >$APP/Contents/PkgInfo
    info_plist $APP/Contents/Info.plist libmapper_PWM_Example pwm_example
    cp -v ../icons/libmapper.icns $APP/Contents/Resources/

    APP=bundles/libmapper_Slider_Example.app
    mkdir -v $APP
    mkdir -v $APP/Contents
    mkdir -v $APP/Contents/MacOS
    mkdir -v $APP/Contents/Resources
    cp -v libmapper-$LIBMAPPER_VERSION/src/.libs/libmapper.*.dylib $APP/Contents/MacOS/libmapper.dylib
    cp -v libmapper-$LIBMAPPER_VERSION/bindings/python/libmapper.py $APP/Contents/MacOS/
    cp -v libmapper-$LIBMAPPER_VERSION/bindings/python/tkgui.py $APP/Contents/MacOS/
    echo 'APPL????' >$APP/Contents/PkgInfo
    info_plist $APP/Contents/Info.plist libmapper_Slider_Example tkgui.py
    cp -v ../icons/libmapper.icns $APP/Contents/Resources/

    APP=bundles/libmapper_Slider_Launcher.app
    mkdir -v $APP
    mkdir -v $APP/Contents
    mkdir -v $APP/Contents/MacOS
    mkdir -v $APP/Contents/Resources
    cp -v libmapper-$LIBMAPPER_VERSION/extra/osx/libmapper_slider_launcher.py $APP/Contents/MacOS/
    echo 'APPL????' >$APP/Contents/PkgInfo
    info_plist $APP/Contents/Info.plist libmapper_Slider_Launcher libmapper_slider_launcher.py
    cp -v ../icons/libmapper.icns $APP/Contents/Resources/

    FRAMEWORK=bundles/mapper.framework
    mkdir -v $FRAMEWORK
    mkdir -v $FRAMEWORK/Versions
    mkdir -v $FRAMEWORK/Versions/$LIBMAPPER_MAJOR
    mkdir -v $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/Libraries
    cp -rv install/lib/libmapper.*.dylib \
        $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/Libraries/
    find $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/Libraries -type f \
        -exec chmod 755 {} \;
    ln -sv $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/Libraries/libmapper.*.dylib \
        $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/Libraries/libmapper.dylib
    mkdir -v $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/Headers
    cp -rv install/include/mapper/* \
        $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/Headers/
    find $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/Headers -type f \
        -exec chmod 644 {} \;
    ln -sv $LIBMAPPER_MAJOR $FRAMEWORK/Versions/Current
    ln -sv Versions/Current/Headers $FRAMEWORK/Headers
    mkdir -v $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/Resources
    ln -sv Versions/Current/Resources $FRAMEWORK/Resources

    cat >$FRAMEWORK/Versions/$LIBMAPPER_MAJOR/Resources/Info.plist <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>English</string>
    <key>CFBundleExecutable</key>
    <string>mapper</string>
    <key>CFBundleIdentifier</key>
    <string>org.idmil.mapper</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>mapper</string>
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
    mkdir $FRAMEWORK/Versions/$LIBMAPPER_MAJOR/Frameworks
    ln -sv Versions/$LIBMAPPER_MAJOR/Frameworks $FRAMEWORK/Frameworks
    FRAMEWORK=$FRAMEWORK/Versions/$LIBMAPPER_MAJOR/Frameworks/lo.framework
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
