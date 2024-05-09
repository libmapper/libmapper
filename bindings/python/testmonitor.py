#!/usr/bin/env python

import libmapper as mpr
import signal, os

done = False
redraw = True

def handler_done(signum, frame):
    global done
    done = True

signal.signal(signal.SIGINT, handler_done)
signal.signal(signal.SIGTERM, handler_done)

graph = mpr.Graph()

def on_event(type, obj, event):
    global redraw
    redraw = True

graph.add_callback(on_event)

while not done:
    graph.poll(500)
    if redraw:
        # clear screen?
        print('\033c', end = '', flush = True)
        # print title
        print('testmonitor.py (libmapper v' + mpr.__version__ + ')')
        # print graph
        graph.print()
        redraw = False

print('freeing graph')
graph.free()
print('done')
