#!/usr/bin/env python3

# This script executes the manylinux Docker images in order to build each supported
# platform wheel.

import os
import subprocess
import multiprocessing
from pprint import pprint

def platforms():
    ARCHES = ['x86_64', 'i686'] # WIP: 'aarch64', 'armv7l', 'ppc64', 'ppc64le', 's390x'
    BASES = ['manylinux2010',  'manylinux2014', 'manylinux_2_24']
    for base in BASES:
        for arch in ARCHES:
            if base == 'manylinux2010' and arch not in ['x86_64', 'i686']:
                continue
            yield f'{base}_{arch}'

def build_wheel(platform):
    cmd = ['docker', 'run', '--rm', '-e', f'PLAT={platform}',
           '-v', f'{os.getcwd()}:/package',
           f'quay.io/pypa/{platform}',
           '/package/bindings/python/manylinux.sh']
    print(' '.join(cmd))
    subprocess.check_call(cmd, stdout=open('/dev/null', 'wb'))
    return platform

with multiprocessing.Pool(8) as pool:
    result = list(pool.map(build_wheel, platforms()))
print('Wheels built for:')
pprint(result)
