#!/usr/bin/env python

from __future__ import print_function
import sys, random, mapper as mpr

def h(sig, event, id, val, timetag):
    print('  handler got', sig['name'], '=', val, 'at time', timetag.get_double())

mins = [0,0,0,0,0,0,0,0,0,0]
maxs = [1,1,1,1,1,1,1,1,1,1]

src = mpr.device("py.testvector.src")
outsig = src.add_signal(mpr.DIR_OUT, "outsig", 10, mpr.INT32, None, mins, maxs)

dest = mpr.device("py.testvector.dst")
insig = dest.add_signal(mpr.DIR_IN, "insig", 10, mpr.FLT, None, mins, maxs, None, h)

while not src.ready or not dest.ready:
    src.poll(10)
    dest.poll(10)

map = mpr.map(outsig, insig)
map.push()

while not map.ready:
    src.poll(10)
    dest.poll(10)

for i in range(100):
    outsig.set_value([i, i+1, i+2, i+3, i+4, i+5, i+6, i+7, i+8, i+9])
    dest.poll(100)
    src.poll(0)
