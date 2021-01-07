#!/usr/bin/env python

from __future__ import print_function
import sys, random, mapper as mpr

def h(sig, event, id, val, time):
    print('  handler got', sig['name'], '=', val, 'at time', time.get_double())

srcs = [mpr.device("py.testconvergent.src"),
        mpr.device("py.testconvergent.src"),
        mpr.device("py.testconvergent.src")]
outsigs = [srcs[0].add_signal(mpr.DIR_OUT, "outsig", 1, mpr.INT32),
           srcs[1].add_signal(mpr.DIR_OUT, "outsig", 1, mpr.INT32),
           srcs[2].add_signal(mpr.DIR_OUT, "outsig", 1, mpr.INT32)]

dest = mpr.device("py.testconvergent.dst")
insig = dest.add_signal(mpr.DIR_IN, "insig", 1, mpr.FLT, None, None, None, None, h)

while not srcs[0].ready or not srcs[1].ready or not srcs[2].ready or not dest.ready:
    srcs[2].poll(10)
    srcs[0].poll(10)
    srcs[1].poll(10)
    dest.poll(10)

map = mpr.map(outsigs, insig)
if not map:
    print('error: map not created')
else:
    map['expr'] = "y=x0+_x1+_x2"
    map.push()

    while not map.ready:
        srcs[0].poll(10)
        srcs[1].poll(10)
        srcs[2].poll(10)
        dest.poll(10)

    for i in range(100):
        for j in range(3):
            outsigs[j].set_value(j*50-i)
            srcs[j].poll(0)
        dest.poll(10)
