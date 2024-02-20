#!/usr/bin/env python

import libmapper as mpr

print('starting testmapfromstring.py')
print('libmapper version:', mpr.__version__, 'with' if mpr.has_numpy() else 'without', 'numpy support')

src = mpr.Device("py.testmapfromstr.src")
outsig = src.add_signal(mpr.Signal.Direction.OUTGOING, "outsig", 1, mpr.Type.INT32)

dest = mpr.Device("py.testmapfromstr.dst")
insig = dest.add_signal(mpr.Signal.Direction.INCOMING, "insig", 1, mpr.Type.FLOAT,
                        None, None, None, None,
                        lambda s, e, i, v, t: print('signal', s['name'], 'got value',
                                                    v, 'at time', t.get_double()),
                        mpr.Signal.Event.UPDATE)

while not src.ready or not dest.ready:
    src.poll(10)
    dest.poll(10)

map = mpr.Map("%y=(%x+100)*2", insig, outsig)
map.push()

while not map.ready:
    src.poll(10)
    dest.poll(10)

for i in range(100):
    outsig.set_value(i)
    dest.poll(10)
    src.poll(0)

print('freeing devices')
src.free()
dest.free()
print('done')
