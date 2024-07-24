#!/usr/bin/env python

import libmapper as mpr

print('starting test.py')
print('libmapper version:', mpr.__version__, 'with' if mpr.has_numpy() else 'without', 'numpy support')

start = mpr.Time()

def h(sig, event, id, val, time):
    try:
        print(sig[mpr.Property.NAME], 'instance', id, 'got', val, 'at T+%.2f' % (time-start).get_double(), 'sec')
    except:
        print('exception')

def setup(d):
    sig = d.add_signal(mpr.Signal.Direction.INCOMING, "freq", 1, mpr.Type.INT32, "Hz", 0, 100, None, h)

    while not d.ready:
        d.poll(10)

    print('device name', d['name'])
    print('device status', d.get_status())
    print('device port', d['port'])
    print('device ordinal', d['ordinal'])

    graph = d.graph()
    print('network ip', graph.address)
    print('network interface', graph.interface)

    d.set_properties({"testInt":5, "testFloat":12.7, "testString":["test","foo"],
                      "removed1":"shouldn't see this"})
    d['testInt'] = 7
    d.set_properties({"removed1":None, "removed2":"test"})
    d.remove_property("removed1")

    print('Printing', d.num_properties, 'properties:')
    for key, value in list(d.properties.items()):
        print('  ', key, ':', value)

    print('device properties:', d.properties)

    if 'name' in sig:
        print("signal has property 'name'")
    else:
        print("error: did not find 'name' property")

    if 'dummy' in sig:
        print("error: signal has property 'dummy'")
    else:
        print("did not find 'dummy' property (expected)")

    print('signal name:', sig['name'])
    print('signal direction:', sig['direction'])
    print('signal length:', sig['length'])
    print('signal type:', sig['type'])
    print('signal unit:', sig['unit'])
    print('signal minimum:', sig['minimum'])
    sig['minimum'] = 34.0
    print('signal minimum:', sig['minimum'])
    sig['minimum'] = 12
    print('signal minimum:', sig['minimum'])
    print('signal minimum using Property Enum:', sig[mpr.Property.MIN])
    sig['minimum'] = None
    print('signal minimum:', sig['minimum'])

    print('3rd property as a tuple:', sig[2])

    sig.properties['testInt'] = 3

    print('signal properties:', sig.properties)

    sig = d.add_signal(mpr.Signal.Direction.INCOMING, "insig", 4, mpr.Type.INT32, None, None, None, None, h)
    print('signal properties:', sig.properties)
    print('signal status:', sig.get_status())
    sig = d.add_signal(mpr.Signal.Direction.OUTGOING, "outsig", 4, mpr.Type.FLOAT)
    print('signal properties:', sig.properties)
    print('signal status:', sig.get_status())

    # try adding a signal with the same name
    sig = d.add_signal(mpr.Signal.Direction.INCOMING, "outsig", 4, mpr.Type.FLOAT)

    print('setup done!')

dev1 = mpr.Device("py.test1")
setup(dev1)
dev2 = mpr.Device("py.test2")
setup(dev2)

def object_name(type):
    if type is mpr.Type.DEVICE:
        return 'DEVICE'
    elif type is mpr.Type.SIGNAL:
        return 'SIGNAL'
    elif type is mpr.Type.MAP:
        return 'MAP'

def graph_cb(type, object, event):
    print(event.name)
    if type is mpr.Type.DEVICE or type is mpr.Type.SIGNAL:
        print(' ', type.name + ':', object['name'])
    elif type is mpr.Type.MAP:
        print(' MAP:')
        for s in object.signals(mpr.Map.Location.SOURCE):
            print("  src: ", s.device()['name'], ':', s['name'])
        for s in object.signals(mpr.Map.Location.DESTINATION):
            print("  dst: ", s.device()['name'], ':', s['name'])

g = mpr.Graph(mpr.Type.OBJECT)
g.add_callback(graph_cb)

start.now()

for s in dev1.signals():
    print("    ", s['name'])

outsig = dev1.signals().filter("name", "outsig").next()

for s in dev2.signals():
    print("    ", s['name'])

insig = dev2.signals().filter("name", "*insig").next()

for i in range(100):
    dev1.poll(10)
    dev2.poll(10)
    g.poll()
    outsig.set_value([i+1,i+2,i+3,i+4])

    if i==0:
        map = mpr.Map(outsig, insig)
        map['expr'] = 'y=y{-1}+x'
        map.push()

        # test list.__contains__()
        print('new map signal list',
              'contains' if outsig in map.signals() else 'does not contain',
              outsig)
        print('new map source list',
              'contains' if insig in map.signals(mpr.Map.Location.SOURCE) else 'does not contain',
              insig)

#        # test creating multi-source map
#        map = mpr.Map([sig1, sig2], sig3)
#        map.expr = 'y=x0-x1'
#        map.push()

    if i==500:
        print('muting map')
        map['muted'] = True
        map.push()

    if i==800:
        map.release()


print("GRAPH:")
g.print()

ndevs = len(g.devices())
nsigs = len(g.signals())
print(ndevs, 'device' if ndevs == 1 else 'devices', 'and', nsigs, 'signal:' if nsigs == 1 else 'signals:')
for d in g.devices():
    print("  DEVICE:", d['name'], '(synced', mpr.Time().get_double() - d['synced'].get_double(), 'seconds ago)')
    for s in d.signals():
        print("    SIGNAL:", s['name'])

maps = g.maps()
nmaps = len(maps)
print(nmaps, 'map:' if nmaps == 1 else 'maps:')
for m in g.maps():
    for s in m.signals(mpr.Map.Location.SOURCE):
        print("  src: ", s.device()['name'], ':', s['name'])
    for s in m.signals(mpr.Map.Location.DESTINATION):
        print("  dst: ", s.device()['name'], ':', s['name'])

# combining queries
print('signals matching \'out*\' or \'*req\':')
q1 = g.signals().filter("name", "out*")
q1.join(g.signals().filter("name", "*req"))
for i in q1:
    print("    ", i['name'])

tt1 = mpr.Time(0.5)
tt2 = mpr.Time(2.5)
tt3 = mpr.Time(2.5)
print("tt1 != tt2") if tt1 != tt2 else print("timetag error")
print("tt1 < tt2") if tt1 < tt2 else print("timetag error")
print("tt2 == tt3") if tt2 == tt3 else print("timetag error")
tt3 = tt1 + 0.5
print('got tt: ', tt3.get_double())
print(1.6 + tt1)
print('current time:', mpr.Time().get_double())

g.print()

g.free()
dev1.free()
dev2.free()
print('done')
