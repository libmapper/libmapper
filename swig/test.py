#!/usr/bin/env python

from __future__ import print_function
import sys, mapper

start = mapper.timetag()

def h(sig, id, f, tt):
    try:
        print((sig['name'], f, 'at T+', (tt-start).get_double()))
    except:
        print('exception')

def setup(d):
    sig = d.add_signal(mapper.DIR_IN, 1, "freq", 1, mapper.INT32, "Hz", None, None, h)

    while not d.ready:
        d.poll(10)

    print('device name', d['name'])
    print('device port', d['port'])
    print('device ordinal', d['ordinal'])

    graph = d.graph()
    print('network ip', graph.multicast_addr)
    print('network interface', graph.interface)

    d.set_properties({"testInt":5, "testFloat":12.7, "testString":[b"test",b"foo"],
                      "removed1":b"shouldn't see this"})
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

    d.add_signal(mapper.DIR_IN, 1, "insig", 4, mapper.INT32, None, None, None, h)
    d.add_signal(mapper.DIR_OUT, 1, "outsig", 4, mapper.FLOAT)
    print('setup done!')

#check libmapper version
print('using libmapper version', mapper.version)
dev = mapper.device("test")
setup(dev)

def object_name(type):
    if type is mapper.OBJ_DEVICE:
        return 'DEVICE'
    elif type is mapper.OBJ_SIGNAL:
        return 'SIGNAL'
    elif type is mapper.OBJ_MAP:
        return 'MAP'

def graph_cb(type, object, action):
    print(object_name(type),["ADDED", "MODIFIED", "REMOVED", "EXPIRED"][action])
    if type is mapper.OBJ_DEVICE or type is mapper.OBJ_SIGNAL:
        print('  ', object['name'])
    elif type is mapper.OBJ_MAP:
        print('  ', object.source()['name'], '->', object.destination()['name'])

g = mapper.graph(mapper.OBJ_ALL)

g.add_callback(graph_cb)

while not dev.ready:
    dev.poll(10)
    g.poll()

start.now()

outsig = dev.signals().filter("name", "outsig").next()
insig = dev.signals().filter("name", "insig").next()
for i in range(1000):
    dev.poll(10)
    g.poll()
    outsig.update([i+1,i+2,i+3,i+4])
    
    if i==250:
        map = mapper.map(outsig, insig)
        map['expression'] = 'y=y{-1}+x'
        map.source()['minimum'] = [1,2,3,4]
        map.push()

#        # test creating multi-source map
#        map = mapper.map([sig1, sig2], sig3)
#        map.expression = 'y=x0-x1'
#        map.push()

    if i==500:
        print('muting map')
        map.source()['minimum'] = [10,11,12,13]
        map['muted'] = True
        map['expression'] = 0
        map.push()

    if i==800:
        map.release()

print(g.devices().length(), 'devices and', g.signals().length(), 'signals:')
for d in g.devices():
    print("  ", d['name'], '(synced', mapper.timetag().get_double() - d['synced'].get_double(), 'seconds ago)')
    for s in d.signals():
        print("    ", s['name'])

print(g.maps().length(), 'maps:')
for m in g.maps():
    print("    ", m.source().device()['name'], ':', \
        m.source()['name'],\
        '->', m.destination().device()['name'], ':', m.destination()['name'])

# combining queries
print('signals matching \'out*\' or \'*req\':')
q1 = g.signals().filter("name", "out*")
q1.join(g.signals().filter("name", "*req"))
for i in q1:
    print("    ", i['name'])

tt1 = mapper.timetag(0.5)
tt2 = mapper.timetag(2.5)
tt3 = tt1 + 0.5
print('got tt: ', tt3.get_double())
print(1.6 + tt1)
print('current time:', mapper.timetag().get_double())
