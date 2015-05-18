#!/usr/bin/env python

import sys, mapper

def sig_h(sig, id, f, timetag):
    try:
        print sig.name, f
    except:
        print 'exception'
        print sig, f

def connect_h(dev, sig, con, direction, action):
    try:
        print '-->', dev['name']+'/'+sig['name'], 'added' if action == mapper.MDEV_LOCAL_ESTABLISHED else 'removed', 'connection from', con['destination']['name'] if direction == mapper.DI_OUTGOING else con['sources']['name']
    except:
        print 'exception'
        print dev
        print action

src = mapper.device("src")
src.set_connection_callback(connect_h)
outsig = src.add_output("outsig", 1, 'f', None, 0, 1000)

dest = mapper.device("dest")
dest.set_connection_callback(connect_h)
insig = dest.add_input("insig", 1, 'f', None, 0, 1, sig_h)

while not src.ready() or not dest.ready():
    src.poll()
    dest.poll(10)

monitor = mapper.monitor()
monitor.connect('%s/%s' %(src.name, outsig.name),
                '%s/%s' %(dest.name, insig.name),
                {'calibrate': 1})
while not src.num_connections_out:
    src.poll(10)
    dest.poll(10)

for i in range(10):
    src.poll(10)
    dest.poll(10)

monitor.disconnect('%s/%s' %(src.name, outsig.name),
                   '%s/%s' %(dest.name, insig.name))

for i in range(10):
    src.poll(10)
    dest.poll(10)
