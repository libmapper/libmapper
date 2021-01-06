#!/usr/bin/env python

from __future__ import print_function
import sys, mapper as mpr

start = mpr.time()

def h(sig, event, id, val, time):
    try:
        print(sig[mpr.PROP_NAME], 'got', val, 'at T+%.2f' % (time-start).get_double(), 'sec')
    except:
        print('exception')

def setup(d):
    sig = d.add_signal(mpr.DIR_IN, "freq", 1, mpr.INT32, "Hz", None, None, None, h)

    while not d.ready:
        d.poll(10)

    print('device name', d['name'])
    print('device port', d['port'])
    print('device ordinal', d['ordinal'])

    graph = d.graph()
    print('network ip', graph.address)
    print('network interface', graph.interface)

    d.set_properties({"testInt":5, "testFloat":12.7, "testString":["test",b"foo"],
                      "removed1":"shouldn't see this"})
    d['testInt'] = 7
#    d.set_properties({"removed1":None, "removed2":"test"})
    d.remove_property("removed1")

    print('Printing', d.num_properties, 'properties:')
    for key, value in list(d.properties.items()):
        print('  ', key, ':', value)

    print('device properties:', d.properties)

    print('signal name', sig['name'])
    print('signal direction', sig['direction'])
    print('signal length', sig['length'])
    print('signal type', sig['type'])
    print('signal unit', sig['unit'])
    print('signal minimum', sig['minimum'])
    sig['minimum'] = 34.0
    print('signal minimum', sig['minimum'])
    sig['minimum'] = 12
    print('signal minimum', sig['minimum'])
    sig['minimum'] = None
    print('signal minimum', sig['minimum'])

    sig.properties['testInt'] = 3

    print('signal properties:', sig.properties)

    d.add_signal(mpr.DIR_IN, "insig", 4, mpr.INT32, None, None, None, None, h)
    d.add_signal(mpr.DIR_OUT, "outsig", 4, mpr.FLT)
    print('setup done!')

#check libmapper version
print('using libmapper version', mpr.version)
dev1 = mpr.device("py.test1")
setup(dev1)
dev2 = mpr.device("py.test2")
setup(dev2)

def object_name(type):
    if type is mpr.DEV:
        return 'DEVICE'
    elif type is mpr.SIG:
        return 'SIGNAL'
    elif type is mpr.MAP:
        return 'MAP'

def graph_cb(type, object, action):
    print(object_name(type),["ADDED", "MODIFIED", "REMOVED", "EXPIRED"][action])
    if type is mpr.DEV or type is mpr.SIG:
        print('  ', object['name'])
    elif type is mpr.MAP:
        for s in object.signals(mpr.LOC_SRC):
            print("  src: ", s.device()['name'], ':', s['name'])
        for s in object.signals(mpr.LOC_DST):
            print("  dst: ", s.device()['name'], ':', s['name'])

g = mpr.graph(mpr.OBJ)

g.add_callback(graph_cb)

while not dev1.ready or not dev2.ready:
    dev1.poll(10)
    dev2.poll(10)
    g.poll()

start.now()

outsig = dev1.signals().filter("name", "outsig").next()
insig = dev2.signals().filter("name", "insig").next()
for i in range(1000):
    dev1.poll(10)
    dev2.poll(10)
    g.poll()
    outsig.set_value([i+1,i+2,i+3,i+4])

    if i==250:
        map = mpr.map(outsig, insig)
        map['expr'] = 'y=y{-1}+x'
        map.push()

#        # test creating multi-source map
#        map = mpr.map([sig1, sig2], sig3)
#        map.expr = 'y=x0-x1'
#        map.push()

    if i==500:
        print('muting map')
        map['muted'] = True
        map.push()

    if i==800:
        map.release()

ndevs = g.devices().length()
nsigs = g.signals().length()
print(ndevs, 'device' if ndevs is 1 else 'devices', 'and', nsigs, 'signal:' if nsigs is 1 else 'signals:')
for d in g.devices():
    print("  ", d['name'], '(synced', mpr.time().get_double() - d['synced'].get_double(), 'seconds ago)')
    for s in d.signals():
        print("    ", s['name'])

maps = g.maps()
nmaps = maps.length()
print(nmaps, 'map:' if nmaps is 1 else 'maps:')
for m in g.maps():
    for s in m.signals(mpr.LOC_SRC):
        print("  src: ", s.device()['name'], ':', s['name'])
    for s in m.signals(mpr.LOC_DST):
        print("  dst: ", s.device()['name'], ':', s['name'])

# combining queries
print('signals matching \'out*\' or \'*req\':')
q1 = g.signals().filter("name", "out*")
q1.join(g.signals().filter("name", "*req"))
for i in q1:
    print("    ", i['name'])

tt1 = mpr.time(0.5)
tt2 = mpr.time(2.5)
tt3 = tt1 + 0.5
print('got tt: ', tt3.get_double())
print(1.6 + tt1)
print('current time:', mpr.time().get_double())
