#!/usr/bin/env python

from __future__ import print_function
import sys, mapper

def h(sig, id, f, timetag):
    try:
        print('--> source received', f)
    except:
        print('exception')
        print(sig, f)

src = mapper.device("src")
outsig = src.add_signal(mapper.DIR_OUT, 1, "outsig", 1, mapper.FLOAT, None, 0, 1000)
outsig.set_callback(h)

dest = mapper.device("dest")
insig = dest.add_signal(mapper.DIR_IN, 1, "insig", 1, mapper.FLOAT, None, 0, 1)

while not src.ready or not dest.ready:
    src.poll(10)
    dest.poll(10)

map = mapper.map(insig, outsig).push()

while not map.ready:
    src.poll(10)
    dest.poll(10)

for i in range(100):
    print('updating destination to', i, '-->')
    insig.update(i)
    src.poll(10)
    dest.poll(10)
