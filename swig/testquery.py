#!/usr/bin/env python

from __future__ import print_function
import sys, mapper

def h(sig, id, f, timetag):
    try:
        print('--> received query response:', f)
    except:
        print('exception')
        print(sig, f)

src = mapper.device("src")
outsig = src.add_output_signal("outsig", 1, mapper.FLOAT, None, 0, 1000)
outsig.set_callback(h)

dest = mapper.device("dest")
insig = dest.add_input_signal("insig", 1, mapper.FLOAT, None, 0, 1, h)

while not src.ready or not dest.ready:
    src.poll()
    dest.poll(10)

map = mapper.map(outsig, insig)
map.mode = mapper.MODE_LINEAR
map.push()

while not map.ready:
    src.poll(10)
    dest.poll(10)

for i in range(100):
    print('updating destination to', i, '-->')
    insig.update(i)
    outsig.query_remotes()
    src.poll(10)
    dest.poll(10)
