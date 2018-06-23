#!/usr/bin/env python

from __future__ import print_function
import sys, mapper, random

def h(sig, id, val, timetag):
    print('  handler got', sig['name'], '=', val, 'at time', timetag.get_double())

srcs = [mapper.device("src"), mapper.device("src")]
outsigs = [srcs[0].add_signal(mapper.DIR_OUT, 1, "outsig", 1, mapper.INT32),
           srcs[1].add_signal(mapper.DIR_OUT, 1, "outsig", 1, mapper.INT32)]

dest = mapper.device("dest")
insig = dest.add_signal(mapper.DIR_IN, 1, "insig", 1, mapper.FLOAT, None, None, None, h)

while not srcs[0].ready or not srcs[1].ready or not dest.ready:
    srcs[0].poll(10)
    srcs[1].poll(10)
    dest.poll(10)

map = mapper.map(outsigs, insig)
if not map:
    print('error: map not created')
else:
    map['expression'] = "y=x0-_x1"
    map.push()

    while not map.ready:
        srcs[0].poll(10)
        srcs[1].poll(10)
        dest.poll(10)

    for i in range(100):
        outsigs[0].update(i)
        outsigs[1].update(100-i)
        dest.poll(10)
        srcs[0].poll(0)
        srcs[1].poll(0)
