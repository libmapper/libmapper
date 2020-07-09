#!/bin/sh

PY=$1
PYPLATFORM="$($PY -c 'import distutils.util as u; import sys; print("%s-%d.%d"%(u.get_platform(), sys.version_info.major, sys.version_info.minor))')"
ARCHES="$(ls -d build/lib.*/$2*.so)"

# If the Python platform architecture is found, copy that one
for A in $ARCHES
do
    if ls $A | grep -q $PYPLATFORM; then
	      echo $A
	      exit 0
    fi
done

# Otherwise, copy the first one listed.
echo $ARCHES | head -n1
