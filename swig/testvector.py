#!/usr/bin/env python

import sys, mapper, random

def h(sig, id, f, timetag):
    print '  handler got', sig.name, '=', f, 'at time', timetag

mins = [0,0,0,0,0,0,0,0,0,0]
maxs = [1,1,1,1,1,1,1,1,1,1]

src = mapper.device("src")
outsig = src.add_output("/outsig1", 10, 'i', None, mins, maxs)

dest = mapper.device("dest")
insig = dest.add_input("/insig1", 10, 'f', None, mins, maxs, h)

while not src.ready() or not dest.ready():
    src.poll()
    dest.poll(10)

monitor = mapper.monitor()

monitor.link(src, dest)
while not src.num_links_out:
    src.poll(10)
    dest.poll(10)
monitor.connect(outsig, insig, {'mode': mapper.MO_LINEAR})
monitor.poll()

for i in range(100):
    outsig.update([0, 1, 2, 3, 4, 5, 6, 7, 8, 9])
    dest.poll(100)
    src.poll(0)
