#!/usr/bin/env python

import sys, mapper, random

def h(sig, id, f, timetag):
    print '  handler got', sig.name, '=', f, 'at time', timetag

src = mapper.device("src")
outsig = src.add_output("/outsig1", 10, 'i', None, 0, 1)

dest = mapper.device("dest")
insig = dest.add_input("/insig1", 10, 'f', None, 0, 1, h)

while not src.ready() or not dest.ready():
    src.poll()
    dest.poll(10)

monitor = mapper.monitor()

monitor.link('%s' %src.name, '%s' %dest.name)
monitor.connect('%s%s' %(src.name, outsig.name),
                '%s%s' %(dest.name, insig.name),
                {'mode': mapper.MO_LINEAR})

for i in range(100):
    outsig.update([0, 1, 2, 3, 4, 5, 6, 7, 8, 9])
    dest.poll(100)
    src.poll(0)
