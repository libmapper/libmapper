#!/usr/bin/env python

import sys, mapper

def link_h(dev, link, action):
    try:
        print '-->', dev['name'], 'added' if action == mapper.MDEV_LOCAL_ESTABLISHED else 'removed', 'link from', link['dest_name'] if dev['name'] == link['src_name'] else link['src_name']
    except:
        print 'exception'
        print dev
        print link
        print action

def connect_h(dev, link, sig, con, action):
    try:
        print '-->', dev['name'], 'added' if action == mapper.MDEV_LOCAL_ESTABLISHED else 'removed', 'connection from', link['dest_name']+con['dest_name'] if dev['name'] == link['src_name'] else link['src_name']+con['src_name']
    except:
        print 'exception'
        print dev
        print link
        print action

src = mapper.device("src", 9000)
src.set_link_callback(link_h)
src.set_connection_callback(connect_h)
outsig = src.add_output("/outsig", 1, 'f', None, 0, 1000)

dest = mapper.device("dest", 9000)
dest.set_link_callback(link_h)
dest.set_connection_callback(connect_h)
insig = dest.add_input("/insig", 1, 'f', None, 0, 1)

while not src.ready() or not dest.ready():
    src.poll()
    dest.poll(10)

monitor = mapper.monitor()

monitor.link('%s' %src.name, '%s' %dest.name)
monitor.connect('%s%s' %(src.name, outsig.name),
                '%s%s' %(dest.name, insig.name),
                {'mode': mapper.MO_REVERSE})
monitor.poll()

for i in range(10):
    src.poll(10)
    dest.poll(10)

monitor.disconnect('%s%s' %(src.name, outsig.name),
                   '%s%s' %(dest.name, insig.name))
monitor.unlink('%s' %src.name, '%s' %dest.name)

for i in range(10):
    src.poll(10)
    dest.poll(10)
