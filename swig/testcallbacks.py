#!/usr/bin/env python

import sys, mapper

def sig_h(sig, id, f, timetag):
    try:
        print sig.name, f
    except:
        print 'exception'
        print sig, f

def action_name(action):
    if action is mapper.ADDED:
        return 'ADDED'
    elif action is mapper.MODIFIED:
        return 'MODIFIED'
    elif action is mapper.REMOVED:
        return 'REMOVED'
    elif action is mapper.EXPIRED:
        return 'EXPIRED'

def map_h(map, action):
    try:
        print 'map', map.source().signal().name, '->', map.destination().signal().name, action_name(action)
#        print '-->', dev['name']+'/'+sig['name'], 'added' if action == mapper.MDEV_LOCAL_ESTABLISHED else 'removed', 'mapping from', map['destination']['name'] if direction == mapper.DI_OUTGOING else map['sources']['name']
    except:
        print 'exception'
        print map
        print action

src = mapper.device("src")
src.set_map_callback(map_h)
outsig = src.add_output_signal("outsig", 1, 'f', None, 0, 1000)

dest = mapper.device("dest")
dest.set_map_callback(map_h)
insig = dest.add_input_signal("insig", 1, 'f', None, 0, 1, sig_h)

while not src.ready() or not dest.ready():
    src.poll()
    dest.poll(10)

map = mapper.map(outsig, insig)
map.source().calibrate = 1
map.push()

while not map.ready():
    src.poll(100)
    dest.poll(100)

map.release()

for i in range(10):
    src.poll(10)
    dest.poll(10)
