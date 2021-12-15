#!/usr/bin/env python

from __future__ import print_function
import sys, libmapper as mpr

def h(sig, event, id, val, time):
    try:
        print('--> source received', val)
    except:
        print('exception')
        print(sig, val)

src = mpr.Device("py.testreverse.src")
outsig = src.add_signal(mpr.Direction.OUTGOING, "outsig", 1, mpr.Type.FLOAT, None, 0, 1000)
outsig.set_callback(h)

dest = mpr.Device("py.testreverse.dst")
insig = dest.add_signal(mpr.Direction.INCOMING, "insig", 1, mpr.Type.FLOAT, None, 0, 1)

while not src.ready or not dest.ready:
    src.poll(10)
    dest.poll(10)

map = mpr.Map(insig, outsig).push()

while not map.ready:
    src.poll(10)
    dest.poll(10)

for i in range(100):
    print('updating destination to', i, '-->')
    insig.set_value(i)
    if i == 50:
        outsig[mpr.Property.DIRECTION] = mpr.Direction.INCOMING;
    src.poll(10)
    dest.poll(10)
