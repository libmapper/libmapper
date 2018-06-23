#!/usr/bin/env python

from __future__ import print_function
import sys, mapper

def sig_h(sig, id, f, timetag):
    try:
        print(sig['name'], f)
    except:
        print('exception')
        print(sig, f)

def action_name(action):
    if action is mapper.ADDED:
        return 'ADDED'
    elif action is mapper.MODIFIED:
        return 'MODIFIED'
    elif action is mapper.REMOVED:
        return 'REMOVED'
    elif action is mapper.EXPIRED:
        return 'EXPIRED'

def device_h(type, device, action):
    try:
        print('device', device['name'], action_name(action))
    except:
        print('exception')
        print(device)
        print(action_name(action))

def map_h(type, map, action):
    try:
        print('map', map.source()['name'], '->', map.destination()['name'], action_name(action))
    except:
        print('exception')
        print(map)
        print(action_name(action))

src = mapper.device("src")
src.graph().add_callback(device_h, mapper.OBJ_DEVICE)
src.graph().add_callback(map_h, mapper.OBJ_MAP)
outsig = src.add_signal(mapper.DIR_OUT, 1, "outsig", 1, mapper.FLOAT, None, 0, 1000)

dst = mapper.device("dst")
dst.graph().add_callback(map_h, mapper.OBJ_MAP)
insig = dst.add_signal(mapper.DIR_IN, 1, "insig", 1, mapper.FLOAT, None, 0, 1, sig_h)

while not src.ready or not dst.ready:
    src.poll()
    dst.poll(10)

map = mapper.map(outsig, insig)
map.source()['calibrate'] = True
map.push()

while not map.ready:
    src.poll(100)
    dst.poll(100)

for i in range(100):
    src.poll(10)
    dst.poll(10)

map.release()

for i in range(100):
    src.poll(10)
    dst.poll(10)
