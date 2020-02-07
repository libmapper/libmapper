#!/usr/bin/env python

from __future__ import print_function

import pwm
import time
try:
    import Tkinter as tkinter
except:
    import tkinter
import sys, os

try:
    import mpr
except:
    try:
        # Try the "swig" directory, relative to the location of this
        # program, which is where it should be if the module has been
        # compiled but not installed.
        sys.path.append(
            os.path.join(os.path.join(os.getcwd(),
                                      os.path.dirname(sys.argv[0])),
                         '../../swig'))
        import mpr
    except:
        print('Error importing libmpr module.')
        sys.exit(1)

def main():
    if pwm.run_synth()==0:
        print("Error running synth.")
        return

    try:
        window = tkinter.Tk()
        window.title("libmpr PWM synth demo")

        name = [False, tkinter.StringVar()]
        name[1].set("Waiting for device name...")
        label = tkinter.Label(window, textvariable=name[1])
        label.pack()

        gain = tkinter.Scale(window, from_=0, to=100, label='gain',
                             orient=tkinter.HORIZONTAL, length=300,
                             command=lambda n: pwm.set_gain(float(n)/100.0))
        gain.pack()

        freq = tkinter.Scale(window, from_=0, to=1000, label='freq',
                             orient=tkinter.HORIZONTAL, length=300,
                             command=lambda n: pwm.set_freq(float(n)))
        freq.pack()

        duty = tkinter.Scale(window, from_=0, to=100, label='duty',
                             orient=tkinter.HORIZONTAL, length=300,
                             command=lambda n: pwm.set_duty(float(n)/100.0))
        duty.pack()

        dev = mpr.device("tk_pwm")

        dev.add_signal(mpr.DIR_IN, "gain", 1, mpr.FLT, None, 0, 100, 0, lambda s,i,n,t: gain.set(n))
        dev.add_signal(mpr.DIR_IN, "freq", 1, mpr.FLT, "Hz", 0, 1000, 0, lambda s,i,n,t: freq.set(n))
        dev.add_signal(mpr.DIR_IN, "duty", 1, mpr.FLT, None, 0, 100, 0, lambda s,i,n,t: duty.set(n))

        def do_poll():
            dev.poll(0)
            if not name[0] and dev.ready:
                name[0] = True
                name[1].set('Device name: %s, listening on port %d'
                            %(dev.name, dev.port))
            window.after(5, do_poll)

        do_poll()
        window.mainloop()

    finally:
        pwm.stop_synth()

main()
