#!/bin/sh

# This script is intended to be run under a "manylinux" Docker distribution for building
# Python wheels.

if [ -z "$PLAT" ]; then
    echo No manylinux platform was defined in \$PLAT
    exit 1
fi

set -e
set -x

ROOT=$(readlink -f $(dirname $0)/../..)
cd $ROOT

TMP=$(mktemp -d)
INST=$TMP/inst

(
    echo === Building liblo
    TAR=$PWD/liblo-0.35.tar.gz
    if ! (echo 'fbe37b3e35f9dcdc4f90acfe513cc679  liblo-0.35.tar.gz'  | md5sum -c -); then
        curl -L -O https://github.com/radarsat1/liblo/releases/download/0.35/liblo-0.35.tar.gz
        echo 'fbe37b3e35f9dcdc4f90acfe513cc679  liblo-0.35.tar.gz' | md5sum -c -
    fi
    cd $TMP
    tar -xzf $TAR
    cd liblo-0.35
    ./configure --host=$HOST --prefix=$INST --disable-tests --disable-tools --disable-examples --disable-doc \
        || (cat config.log; echo "Error."; false)
    make clean
    make install -j4
    find $INST
) || exit 1

echo === Building libmapper
mkdir -p $TMP/libmapper
cd $TMP/libmapper
PKG_CONFIG_PATH=$INST/lib/pkgconfig $ROOT/configure \
  --prefix=$INST --disable-tests --disable-audio --disable-java --disable-csharp --disable-python \
  --libdir=$TMP/libmapper/bindings/python/libmapper || bash -i
make clean
make install -j4
make clean
PKG_CONFIG_PATH=$INST/lib/pkgconfig $ROOT/configure \
  --prefix=$INST --disable-tests --disable-audio --disable-java --disable-csharp \
  --libdir=$TMP/libmapper/bindings/python/libmapper || bash -i
make
echo === copying README.md
cp $ROOT/bindings/python/README.md $TMP/libmapper/bindings/python/README.md

echo === Building wheel
python3.8 -m ensurepip --upgrade
python3.8 -m pip wheel -w $TMP/wheelhouse $TMP/libmapper/bindings/python
unzip -t $TMP/wheelhouse/*.whl
for WHL in $TMP/wheelhouse/*.whl; do
    auditwheel repair $WHL --plat "$PLAT" -w $ROOT/wheelhouse || bash -i
done
