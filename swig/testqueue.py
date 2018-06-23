#!/usr/bin/env python

from __future__ import print_function
import sys, mapper, random

def h(sig, id, f, tt):
    print('     handler got', sig['name'], '=', f, 'at time', tt.get_double())

src = mapper.device("src")
outsig1 = src.add_signal(mapper.DIR_OUT, 1, "outsig1", 1, mapper.INT32, None, 0, 1000)
outsig2 = src.add_signal(mapper.DIR_OUT, 1, "outsig2", 1, mapper.INT32, None, 0, 1000)

dest = mapper.device("dest")
insig1 = dest.add_signal(mapper.DIR_IN, 1, "insig1", 1, mapper.FLOAT, None, 0, 1, h)
insig2 = dest.add_signal(mapper.DIR_IN, 1, "insig2", 1, mapper.FLOAT, None, 0, 1, h)

while not src.ready or not dest.ready:
    src.poll()
    dest.poll(10)

map1 = mapper.map(outsig1, insig1)
map1.push()

map2 = mapper.map(outsig2, insig2)
map2.push()

while not map1.ready or not map2.ready:
    src.poll()
    dest.poll(10)

for i in range(50):
    now = src.start_queue()
    print('Updating output signals to', i, 'at time', now.get_double())
    outsig1.update(i)
    outsig2.update(i)
    src.send_queue(now)
    dest.poll(100)
    src.poll(0)
