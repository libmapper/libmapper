#!/usr/bin/env python

import libmapper as mpr

print('starting testconvergent.py')
print('libmapper version:', mpr.__version__, 'with' if mpr.has_numpy() else 'without', 'numpy support')

def h(sig, event, id, val, time):
    print('  handler got', sig['name'], '=', val, 'at', time)

srcs = [mpr.Device("py.testconvergent.src1"),
        mpr.Device("py.testconvergent.src2"),
        mpr.Device("py.testconvergent.src3")]
outsigs = [srcs[0].add_signal(mpr.Signal.Direction.OUTGOING, "outsig", 1, mpr.Type.INT32),
           srcs[1].add_signal(mpr.Signal.Direction.OUTGOING, "outsig", 1, mpr.Type.INT32),
           srcs[2].add_signal(mpr.Signal.Direction.OUTGOING, "outsig", 1, mpr.Type.INT32)]

dest = mpr.Device("py.testconvergent.dst")
insig = dest.add_signal(mpr.Signal.Direction.INCOMING, "insig", 1, mpr.Type.FLOAT, None, None, None, None, h)

while not srcs[0].ready or not srcs[1].ready or not srcs[2].ready or not dest.ready:
    srcs[0].poll(100)
    srcs[1].poll(100)
    srcs[2].poll(100)
    dest.poll(100)
    print("  registered: [",srcs[0].ready,",",srcs[1].ready,",",srcs[2].ready,"]->[",dest.ready,"]")

map = mpr.Map(outsigs, insig)
if not map:
    print('error: map not created')
else:
    map['expr'] = "y=x$0+_x$1+_x$2"
    map.push()

    while not map.ready:
        srcs[0].poll(10)
        srcs[1].poll(10)
        srcs[2].poll(10)
        dest.poll(10)

    for i in range(100):
        for j in range(3):
            outsigs[j].set_value(j*50-i)
            srcs[j].poll(0)
        dest.poll(10)

print('freeing devices')
for j in range(3):
    srcs[j].free()
dest.free()
print('done')
