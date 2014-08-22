#!/usr/bin/env python

import sys, mapper

def h(sig, id, f, timetag):
    try:
        print '--> source received', f
    except:
        print 'exception'
        print sig, f

src = mapper.device("src")
outsig = src.add_output("/outsig", 1, 'f', None, 0, 1000)
outsig.set_callback(h)

dest = mapper.device("dest")
insig = dest.add_input("/insig", 1, 'f', None, 0, 1)

while not src.ready() or not dest.ready():
    src.poll()
    dest.poll(10)

monitor = mapper.monitor()

monitor.link(src, dest)
while not src.num_links_out:
    src.poll()
    dest.poll(10)
monitor.connect(outsig, insig, {'mode': mapper.MO_REVERSE})
monitor.poll()

for i in range(100):
    print 'updating destination to', i, '-->'
    insig.update(i)
    src.poll(10)
    dest.poll(10)
