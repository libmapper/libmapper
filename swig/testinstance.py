#!/usr/bin/env python

from __future__ import print_function
import sys, mapper, random

def h(sig, id, f, timetag):
    try:
        print('--> destination instance', id, 'got', f)
        if not f:
            print('retiring destination instance', id)
            sig.release_instance(id)
    except:
        print('--> exception')
        print(sig, id, f)

def manage_instances(sig, id, flag, timetag):
    try:
        if flag == mapper.INSTANCE_OVERFLOW:
            print('--> OVERFLOW for sig', sig.name, 'instance', id)
    except:
        print('--> exception')

def print_instance_ids():
    phrase = 'active /outsig: ['
    count = outsig.num_active_instances
    for i in range(count):
        phrase += ' '
        phrase += str(outsig.active_instance_id(i))
    phrase += ' ]   '
    phrase += 'active /insig: ['
    count = insig.num_active_instances
    for i in range(count):
        phrase += ' '
        phrase += str(insig.active_instance_id(i))
    phrase += ' ]'
    print(phrase)

src = mapper.device("src")
outsig = src.add_output_signal("outsig", 1, 'f', None, 0, 100)
outsig.reserve_instances(5)

dest = mapper.device("dest")
insig = dest.add_input_signal("insig", 1, 'f', None, 0, 1, h)
insig.remove_instance(0)
insig.reserve_instances([100, 200, 300])
insig.set_instance_stealing_mode(mapper.STEAL_OLDEST)

while not src.ready() or not dest.ready():
    src.poll()
    dest.poll(10)

map = mapper.map(outsig, insig).push()

while not map.ready():
    src.poll(10)
    dest.poll(10)

for i in range(100):
    r = random.randint(0,5)
    id = random.randint(0,5)
    if r == 0:
        print('--> retiring sender instance', id)
        outsig.release_instance(id)
    else:    
        print('--> sender instance', id, 'updated to', i)
        outsig.update_instance(id, i)
    print_instance_ids()
    dest.poll(100)
    src.poll(0)
