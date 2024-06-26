#!/usr/bin/env python

import libmapper as mpr

print('starting testgetvalue.py')
print('libmapper version:', mpr.__version__, 'with' if mpr.has_numpy() else 'without', 'numpy support')

src = mpr.Device("py.testvector.src")
outsig = src.add_signal(mpr.Signal.Direction.OUTGOING, "outsig", 1, mpr.Type.INT32, None, 0, 1, 0)
outsig.reserve_instances(1)

dest = mpr.Device("py.testvector.dst")
insig = dest.add_signal(mpr.Signal.Direction.INCOMING, "insig", 1, mpr.Type.FLOAT, None, 0, 1, 0)
insig.reserve_instances(1)

while not src.ready or not dest.ready:
    src.poll(10)
    dest.poll(10)

map = mpr.Map(outsig, insig)
map[mpr.Property.USE_INSTANCES] = False
map.push()

while not map.ready:
    src.poll(10)
    dest.poll(10)

last_update = 0

for i in range(100):
    outsig.set_value([i, i+1, i+2, i+3, i+4, i+5, i+6, i+7, i+8, i+9])
    dest.poll(10)
    src.poll(10)
    val = insig.get_value()
    print('value:', val)
    if val and val[1] > last_update:
        last_update = val[1]
    else:
        print('  updating locally!')
        insig.set_value(0)
    print('outsig:', outsig.get_value(), 'insig:', insig.get_value())

print('freeing devices')
src.free()
dest.free()
print('done')
