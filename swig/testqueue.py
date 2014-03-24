#!/usr/bin/env python

import sys, mapper, random

def h(sig, id, f, timetag):
    print '     handler got', sig.name, '=', f, 'at time', timetag

src = mapper.device("src")
outsig1 = src.add_output("/outsig1", 1, 'i', None, 0, 1000)
outsig2 = src.add_output("/outsig2", 1, 'i', None, 0, 1000)

dest = mapper.device("dest")
insig1 = dest.add_input("/insig1", 1, 'f', None, 0, 1, h)
insig2 = dest.add_input("/insig2", 1, 'f', None, 0, 1, h)

while not src.ready() or not dest.ready():
    src.poll()
    dest.poll(10)

monitor = mapper.monitor()

monitor.link('%s' %src.name, '%s' %dest.name)
monitor.connect('%s%s' %(src.name, outsig1.name),
                '%s%s' %(dest.name, insig1.name),
                {'mode': mapper.MO_LINEAR})
monitor.connect('%s%s' %(src.name, outsig2.name),
                '%s%s' %(dest.name, insig2.name),
                {'mode': mapper.MO_LINEAR})
monitor.poll()

for i in range(10):
    now = src.now()
    print 'Updating output signals to', i, 'at time', now
    src.start_queue(now)
    outsig1.update(i)
    outsig2.update(i)
    src.send_queue(now)
    dest.poll(100)
    src.poll(0)
