#!/usr/bin/env python

import Tkinter
import sys
import mapper

def on_gui_change(x):
    sig_out.update_scalar(int(x))

def on_mapper_change(sig, x):
    w.set(int(x))

dev = mapper.device("tkgui", 9000)

sig_in = mapper.signal(1, "/signal0", 'i', None, on_mapper_change)
dev.register_input(sig_in)

sig_out = mapper.signal(1, "/signal0", 'i', None, lambda x: x)
dev.register_output(sig_out)

master = Tkinter.Tk()
master.title("libmapper Python GUI demo")

w = Tkinter.Scale(master, from_=0, to=100, label='signal0',
                  orient=Tkinter.HORIZONTAL, length=300,
                  command=on_gui_change)
w.pack()

def do_poll():
    dev.poll(20)
    master.after(5, do_poll)

do_poll()
master.mainloop()
