#!/bin/sh

PYMACHINE="$(python -c 'import platform; print platform.machine()')"
ARCHES="$(ls -d build/lib.*/$1)"

# If the Python platform architecture is found, copy that one
for A in $ARCHES
do
    if file $A | grep -q $PYMACHINE; then
	echo $A
	exit 0
    fi
done

# Otherwise, copy the first one listed.
echo $ARCHES | head -n1
