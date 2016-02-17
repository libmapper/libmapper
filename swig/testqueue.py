#!/usr/bin/env python

import sys, mapper, random

def h(sig, id, f, timetag):
    print '     handler got', sig.name, '=', f, 'at time', timetag

src = mapper.device("src")
outsig1 = src.add_output_signal("outsig1", 1, 'i', None, 0, 1000)
outsig2 = src.add_output_signal("outsig2", 1, 'i', None, 0, 1000)

dest = mapper.device("dest")
insig1 = dest.add_input_signal("insig1", 1, 'f', None, 0, 1, h)
insig2 = dest.add_input_signal("insig2", 1, 'f', None, 0, 1, h)

while not src.ready() or not dest.ready():
    src.poll()
    dest.poll(10)

map1 = mapper.map(outsig1, insig1)
map1.mode = mapper.MODE_LINEAR
map1.push()

map2 = mapper.map(outsig2, insig2)
map2.mode = mapper.MODE_LINEAR
map2.push()

while not map1.ready() or not map2.ready():
    src.poll()
    dest.poll(10)

for i in range(10):
    now = src.now()
    print 'Updating output signals to', i, 'at time', now
    src.start_queue(now)
    outsig1.update(i)
    outsig2.update(i)
    src.send_queue(now)
    dest.poll(100)
    src.poll(0)
