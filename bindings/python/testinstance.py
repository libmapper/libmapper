#!/usr/bin/env python

from __future__ import print_function
import sys, random, libmapper as mpr

def h(sig, event, id, val, time):
    try:
        if event == mpr.Signal.Event.UPDATE:
            print('--> destination instance', id, 'got', val)
        elif event == mpr.Signal.Event.REL_UPSTRM:
            print('--> retiring destination instance', id)
            sig.instance(id).release()
    except:
        print('--> exception!')
        print(sig, event, id, val)

def print_instance_ids():
    phrase = 'active /outsig: ['
    count = outsig.num_instances(mpr.Status.ACTIVE)
    for i in range(count):
        phrase += ' '
        phrase += str(outsig.instance_id(i, mpr.Status.ACTIVE))
    phrase += ' ]   '
    phrase += 'active /insig: ['
    count = insig.num_instances(mpr.Status.ACTIVE)
    for i in range(count):
        phrase += ' '
        phrase += str(insig.instance_id(i, mpr.Status.ACTIVE))
    phrase += ' ]'
    print(phrase)

def print_instance_values():
    phrase = 'active /outsig: ['
    count = outsig.num_instances(mpr.Status.ACTIVE)
    for i in range(count):
        phrase += ' '
        phrase += str(outsig.instance(outsig.instance_id(i, mpr.Status.ACTIVE)).get_value()[0])
    phrase += ' ]   '
    phrase += 'active /insig: ['
    count = insig.num_instances(mpr.Status.ACTIVE)
    for i in range(count):
        phrase += '   '
        phrase += str(insig.instance(insig.instance_id(i, mpr.Status.ACTIVE)).get_value()[0])
    phrase += ' ]'
    print(phrase)

src = mpr.Device("py.testinstance.src")
outsig = src.add_signal(mpr.Direction.OUTGOING, "outsig", 1, mpr.Type.INT32, None, 0, 100, 5)
outsig.reserve_instances(5)
outsig.set_property(mpr.Property.EPHEMERAL, True)

dest = mpr.Device("py.testinstance.dst")
# reserve 0 instances to start so we can use custom indexes
insig = dest.add_signal(mpr.Direction.INCOMING, "insig", 1, mpr.Type.INT32, None, 0, 1, 0, h,
                        mpr.Signal.Event.ALL)
insig.reserve_instances([100, 200, 300])
insig.set_property(mpr.Property.STEALING, mpr.Stealing.OLDEST)
insig.set_property(mpr.Property.EPHEMERAL, True)

while not src.ready or not dest.ready:
    src.poll()
    dest.poll(10)

map = mpr.Map(outsig, insig).push()

while not map.ready:
    src.poll(10)
    dest.poll(10)

for i in range(100):
    r = random.randint(0,5)
    id = random.randint(0,5)
    if r == 0:
        print('--> retiring sender instance', id)
        outsig.instance(id).release()
    else:    
        print('--> sender instance', id, 'updated to', id)
        outsig.instance(id).set_value(id)
    print_instance_ids()
    print_instance_values()
    dest.poll(100)
    src.poll(0)
