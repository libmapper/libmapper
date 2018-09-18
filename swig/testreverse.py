#!/usr/bin/env python

from __future__ import print_function
import sys, mpr

def h(sig, event, id, val, timetag):
    try:
        print('--> source received', val)
    except:
        print('exception')
        print(sig, val)

src = mpr.device("src")
outsig = src.add_signal(mpr.DIR_OUT, 1, "outsig", 1, mpr.FLT, None, 0, 1000)
outsig.set_callback(h)

dest = mpr.device("dest")
insig = dest.add_signal(mpr.DIR_IN, 1, "insig", 1, mpr.FLT, None, 0, 1)

while not src.ready or not dest.ready:
    src.poll(10)
    dest.poll(10)

map = mpr.map(insig, outsig).push()

while not map.ready:
    src.poll(10)
    dest.poll(10)

for i in range(100):
    print('updating destination to', i, '-->')
    insig.set_value(i)
    src.poll(10)
    dest.poll(10)
