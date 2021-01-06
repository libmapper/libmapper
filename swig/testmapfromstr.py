#!/usr/bin/env python

from __future__ import print_function
import sys, random, mapper as mpr

def h(sig, event, id, val, time):
    print('  handler got', sig['name'], '=', val, 'at time', time.get_double())

src = mpr.device("py.testmapfromstr.src")
outsig = src.add_signal(mpr.DIR_OUT, "outsig", 1, mpr.INT32)

dest = mpr.device("py.testmapfromstr.dst")
insig = dest.add_signal(mpr.DIR_IN, "insig", 1, mpr.FLT, None, None, None, None, h)

while not src.ready or not dest.ready:
    src.poll(10)
    dest.poll(10)

map = mpr.map("%y=(%x+100)*2", insig, outsig)
map.push()

while not map.ready:
    src.poll(10)
    dest.poll(10)

for i in range(100):
    outsig.set_value(i)
    dest.poll(100)
    src.poll(0)
