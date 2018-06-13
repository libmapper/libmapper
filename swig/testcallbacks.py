#!/usr/bin/env python

from __future__ import print_function
import sys, mapper

def sig_h(sig, id, f, timetag):
    try:
        print(sig.name, f)
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

def link_h(link, action):
    try:
        print('link', link.device(0).name, '<->', link.device(1).name, action_name(action))
    except:
        print('exception')
        print(link)
        print(action)

def map_h(map, action):
    try:
        print('map', map.source().signal().name, '->', map.destination().signal().name, action_name(action))
    except:
        print('exception')
        print(map)
        print(action)

src = mapper.device("src")
src.set_link_callback(link_h)
src.set_map_callback(map_h)
outsig = src.add_output_signal("outsig", 1, mapper.FLOAT, None, 0, 1000)

dest = mapper.device("dest")
dest.set_link_callback(link_h)
dest.set_map_callback(map_h)
insig = dest.add_input_signal("insig", 1, mapper.FLOAT, None, 0, 1, sig_h)

while not src.ready or not dest.ready:
    src.poll()
    dest.poll(10)

map = mapper.map(outsig, insig)
map.source().calibrate = 1
map.push()

while not map.ready:
    src.poll(100)
    dest.poll(100)

for i in range(100):
    src.poll(10)
    dest.poll(10)

map.release()

for i in range(100):
    src.poll(10)
    dest.poll(10)
