#!/usr/bin/env python

import sys, mapper, random

def h(sig, id, f):
    try:
        print '----> received instance:', id, f
    except:
        print '----> exception'
        print sig, id, f

src = mapper.device("src", 9000)
outsig = src.add_output("/outsig", 1, 'f', None, 0, 1000, 5)

dest = mapper.device("dest", 9000)
insig = dest.add_input("/insig", 1, 'f', None, 0, 1, h, 5)

while not src.ready() or not dest.ready():
    src.poll()
    dest.poll(10)

monitor = mapper.monitor()

monitor.link('%s' %src.name, '%s' %dest.name)
monitor.connect('%s%s' %(src.name, outsig.name),
                '%s%s' %(dest.name, insig.name),
                {'mode': mapper.MO_LINEAR})

for i in range(100):
    r = random.randint(0,5)
    id = random.randint(0,5)
    if r == 0:
        print 'retiring sender instance', id
        outsig.release_instance(id)
    else:    
        print 'updating instance', id, 'to', i
        outsig.update_instance(id, i)
    src.poll(10)
    dest.poll(10)
