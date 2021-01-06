#!/usr/bin/env python

from __future__ import print_function
import sys, mapper as mpr

def sig_h(sig, event, id, val, time):
    try:
        print(sig['name'], val)
    except:
        print('exception')
        print(sig, val)

def action_name(action):
    if action is mpr.OBJ_NEW:
        return 'ADDED'
    elif action is mpr.OBJ_MOD:
        return 'MODIFIED'
    elif action is mpr.OBJ_REM:
        return 'REMOVED'
    elif action is mpr.OBJ_EXP:
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
        print('map:')
        for s in map.signals(mpr.LOC_SRC):
            print("  src: ", s.device()['name'], ':', s['name'])
        for s in map.signals(mpr.LOC_DST):
            print("  dst: ", s.device()['name'], ':', s['name'])
    except:
        print('exception')
        print(map)
        print(action_name(action))

src = mpr.device("py.testcallbacks.src")
src.graph().add_callback(device_h, mpr.DEV)
src.graph().add_callback(map_h, mpr.MAP)
outsig = src.add_signal(mpr.DIR_OUT, "outsig", 1, mpr.FLT, None, 0, 1000)

dst = mpr.device("py.testcallbacks.dst")
dst.graph().add_callback(map_h, mpr.MAP)
insig = dst.add_signal(mpr.DIR_IN, "insig", 1, mpr.FLT, None, 0, 1, None, sig_h)

while not src.ready or not dst.ready:
    src.poll()
    dst.poll(10)

map = mpr.map(outsig, insig)
map['expr'] = "y=linear(x,0,100,0,3)"
map.push()

while not map.ready:
    src.poll(100)
    dst.poll(100)

for i in range(100):
    outsig.set_value(i)
    src.poll(10)
    dst.poll(10)

map.release()

for i in range(100):
    src.poll(10)
    dst.poll(10)
