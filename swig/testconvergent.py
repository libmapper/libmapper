#!/usr/bin/env python

from __future__ import print_function
import sys, mpr, random

def h(sig, event, id, val, timetag):
    print('  handler got', sig['name'], '=', val, 'at time', timetag.get_double())

srcs = [mpr.device("src"), mpr.device("src"), mpr.device("src")]
outsigs = [srcs[0].add_signal(mpr.DIR_OUT, 1, "outsig", 1, mpr.INT32),
           srcs[1].add_signal(mpr.DIR_OUT, 1, "outsig", 1, mpr.INT32),
           srcs[2].add_signal(mpr.DIR_OUT, 1, "outsig", 1, mpr.INT32)]

dest = mpr.device("dest")
insig = dest.add_signal(mpr.DIR_IN, 1, "insig", 1, mpr.FLT, None, None, None, h)

while not srcs[0].ready or not srcs[1].ready or not srcs[2].ready or not dest.ready:
    srcs[2].poll(10)
    srcs[0].poll(10)
    srcs[1].poll(10)
    dest.poll(10)

map = mpr.map(outsigs, insig)
if not map:
    print('error: map not created')
else:
    map['expression'] = "y=x0+_x1+_x2"
    map.push()

    while not map.ready:
        srcs[0].poll(10)
        srcs[1].poll(10)
        srcs[2].poll(10)
        dest.poll(10)

    for i in range(100):
        outsigs[0].set_value(i)
        outsigs[1].set_value(50-i)
        outsigs[2].set_value(100-i)
        dest.poll(10)
        srcs[0].poll(0)
        srcs[1].poll(0)
        srcs[2].poll(0)
