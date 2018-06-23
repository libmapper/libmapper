#!/usr/bin/env python

from __future__ import print_function
import sys, mapper, random

def h(sig, id, val, timetag):
    print('  handler got', sig['name'], '=', val, 'at time', timetag.get_double())

mins = [0,0,0,0,0,0,0,0,0,0]
maxs = [1,1,1,1,1,1,1,1,1,1]

src = mapper.device("src")
outsig = src.add_signal(mapper.DIR_OUT, 1, "outsig", 10, mapper.INT32, None, mins, maxs)

dest = mapper.device("dest")
insig = dest.add_signal(mapper.DIR_IN, 1, "insig", 10, mapper.FLOAT, None, mins, maxs, h)

while not src.ready or not dest.ready:
    src.poll(10)
    dest.poll(10)

map = mapper.map(outsig, insig)
map.push()

while not map.ready:
    src.poll(10)
    dest.poll(10)

for i in range(100):
    outsig.update([i, i+1, i+2, i+3, i+4, i+5, i+6, i+7, i+8, i+9])
    dest.poll(100)
    src.poll(0)
