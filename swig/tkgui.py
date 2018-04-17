#!/usr/bin/env python

import Tkinter
import sys
import mapper

def on_gui_change(x):
    sig_out.update(int(x))

def on_mapper_change(sig, id, x, timetag):
    w.set(int(x))

dev = mapper.device("tkgui")

sig_in = dev.add_input_signal("signal", 1, 'i', None, 0, 100, on_mapper_change)
sig_out = dev.add_output_signal("signal", 1, 'i', None, 0, 100)

master = Tkinter.Tk()
master.title("libmapper Python GUI demo")

name = Tkinter.StringVar()
name.set("Waiting for device name...")
name_known = False
label = Tkinter.Label(master, textvariable=name)
label.pack()

w = Tkinter.Scale(master, from_=0, to=100, label='signal0',
                  orient=Tkinter.HORIZONTAL, length=300,
                  command=on_gui_change)
w.pack()

def do_poll():
    global name_known
    dev.poll(20)
    if dev.ready and not name_known:
        name.set('Device name: %s, listening on port %d'%(dev.name, dev.port))
        name_known = True
    master.after(5, do_poll)

do_poll()
master.mainloop()
