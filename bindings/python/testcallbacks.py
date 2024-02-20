#!/usr/bin/env python

import libmapper as mpr

print('starting testcallbacks.py')
print('libmapper version:', mpr.__version__, 'with' if mpr.has_numpy() else 'without', 'numpy support')

def sig_h(sig, event, id, val, time):
    try:
        print(sig['name'], val)
    except:
        print('exception')
        print(sig, val)

def device_h(type, device, event):
    try:
        print(event.name, 'device', device['name'])
    except:
        print('exception')
        print(device)
        print(event_name(event))

def map_h(type, map, event):
    try:
        print(event.name, 'map:')
        for s in map.signals(mpr.Map.Location.SOURCE):
            print("  src: ", s.device()['name'], ':', s['name'])
        for s in map.signals(mpr.Map.Location.DESTINATION):
            print("  dst: ", s.device()['name'], ':', s['name'])
    except:
        print('exception')
        print(map)
        print(event_name(event))

src = mpr.Device("py.testcallbacks.src")
src.graph().add_callback(device_h, mpr.Type.DEVICE)
src.graph().add_callback(map_h, mpr.Type.MAP)
outsig = src.add_signal(mpr.Signal.Direction.OUTGOING, "outsig", 1, mpr.Type.FLOAT, None, 0, 1000)

dst = mpr.Device("py.testcallbacks.dst")
dst.graph().add_callback(map_h, mpr.Type.MAP)
insig = dst.add_signal(mpr.Signal.Direction.INCOMING, "insig", 1, mpr.Type.FLOAT, None, 0, 1, None, sig_h)

while not src.ready or not dst.ready:
    src.poll()
    dst.poll(10)

map = mpr.Map(outsig, insig)
map['expr'] = "y=linear(x,0,100,0,3)"
map.push()

while not map.ready:
    src.poll(100)
    dst.poll(100)

for i in range(100):
    outsig.set_value(i)
    src.poll(10)
    dst.poll(10)

print('removing map')
map.release()

for i in range(100):
    outsig.set_value(i)
    src.poll(10)
    dst.poll(10)

print('freeing devices')
src.free()
dst.free()
print('done')
