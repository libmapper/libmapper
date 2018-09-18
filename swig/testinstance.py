#!/usr/bin/env python

from __future__ import print_function
import sys, mpr, random

def h(sig, event, id, val, timetag):
    try:
        print('--> destination instance', id, 'got', val)
        if not val:
            print('retiring destination instance', id)
            sig.release_instance(id)
    except:
        print('--> exception')
        print(sig, id, val)

def print_instance_ids():
    phrase = 'active /outsig: ['
    count = outsig.num_instances()
    for i in range(count):
        phrase += ' '
        phrase += str(outsig.instance_id(i))
    phrase += ' ]   '
    phrase += 'active /insig: ['
    count = insig.num_instances()
    for i in range(count):
        phrase += ' '
        phrase += str(insig.instance_id(i))
    phrase += ' ]'
    print(phrase)

src = mpr.device("src")
outsig = src.add_signal(mpr.DIR_OUT, 1, "outsig", 1, mpr.FLT, None, 0, 100)
outsig.reserve_instances(5)

dest = mpr.device("dest")
insig = dest.add_signal(mpr.DIR_IN, 1, "insig", 1, mpr.FLT, None, 0, 1, h)
insig.remove_instance(0)
insig.reserve_instances([100, 200, 300])
insig.set_property(mpr.PROP_STEAL_MODE, mpr.STEAL_OLDEST)

while not src.ready or not dest.ready:
    src.poll()
    dest.poll(10)

map = mpr.map(outsig, insig).push()

while not map.ready:
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
        outsig.set_value(id, i)
    print_instance_ids()
    dest.poll(100)
    src.poll(0)
