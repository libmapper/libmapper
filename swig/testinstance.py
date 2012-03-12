#!/usr/bin/env python

import sys, mapper, random

def h(sig, id, f):
    try:
        print '--> destination instance', id, 'got', f
    except:
        print '--> exception'
        print sig, id, f

def print_instance_ids():
    phrase = 'active /outsig: ['
    count = outsig.num_active_instances()
    for i in range(count):
        phrase += ' '
        phrase += str(outsig.active_instance_id(i))
    phrase += ' ]   '
    phrase += 'active /insig: ['
    count = insig.num_active_instances()
    for i in range(count):
        phrase += ' '
        phrase += str(insig.active_instance_id(i))
    phrase += ' ]'
    print phrase

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
        print '--> retiring sender instance', id
        outsig.release_instance(id)
    else:    
        print '--> sender instance', id, 'updated to', i
        outsig.update_instance(id, i)
    print_instance_ids()
    dest.poll(100)
    src.poll(0)
