#!/usr/bin/env python

import libmapper as mpr

print('starting testgetvalue.py')
print('libmapper version:', mpr.__version__, 'with' if mpr.has_numpy() else 'without', 'numpy support')

src = mpr.Device("py.testvector.src")
outsig = src.add_signal(mpr.Direction.OUTGOING, "outsig", 1, mpr.Type.INT32, None, 0, 1)

dest = mpr.Device("py.testvector.dst")
insig = dest.add_signal(mpr.Direction.INCOMING, "insig", 1, mpr.Type.FLOAT, None, 0, 1)

while not src.ready or not dest.ready:
    src.poll(10)
    dest.poll(10)

map = mpr.Map(outsig, insig)
map.push()

while not map.ready:
    src.poll(10)
    dest.poll(10)

for i in range(100):
    outsig.set_value([i, i+1, i+2, i+3, i+4, i+5, i+6, i+7, i+8, i+9])
    dest.poll(10)
    src.poll(10)
    print('insig:', insig.get_value())

print('freeing devices')
src.free()
dest.free()
print('done')
