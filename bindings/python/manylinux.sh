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
    TAR=$PWD/liblo-0.32.tar.gz
    if ! (echo 'a93a7a9da084e6a0937bde6fc324a52a  liblo-0.32.tar.gz'  | md5sum -c -); then
        curl -L -O https://downloads.sourceforge.net/project/liblo/liblo/0.32/liblo-0.32.tar.gz
        echo 'a93a7a9da084e6a0937bde6fc324a52a  liblo-0.32.tar.gz' | md5sum -c -
    fi
    cd $TMP
    tar -xzf $TAR
    cd liblo-0.32
    ./configure --host=$HOST --prefix=$INST --disable-tests --disable-tools --disable-examples \
        || (cat config.log; echo "Error."; false)
    make clean
    make install -j4
    find $INST
) || exit 1

echo === Building libmapper
mkdir -p $TMP/libmapper
cd $TMP/libmapper
PKG_CONFIG_PATH=$INST/lib/pkgconfig $ROOT/configure \
  --prefix=$INST --disable-tests --disable-audio --disable-java \
  --libdir=$TMP/libmapper/bindings/python/libmapper || bash -i
make clean
make install -j4

python3.8 -m pip wheel -w $TMP/wheelhouse $TMP/libmapper/bindings/python
unzip -t $TMP/wheelhouse/*.whl
for WHL in $TMP/wheelhouse/*.whl; do
    auditwheel repair $WHL --plat "$PLAT" -w $ROOT/wheelhouse || bash -i
done
