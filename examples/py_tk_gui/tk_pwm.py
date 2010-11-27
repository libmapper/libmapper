#!/usr/bin/env python

import pwm
import time
import Tkinter
import sys, os

try:
    import mapper
except:
    try:
        # Try the "swig" directory, relative to the location of this
        # program, which is where it should be if the module has been
        # compiled but not installed.
        sys.path.append(
            os.path.join(os.path.join(os.getcwd(),
                                      os.path.dirname(sys.argv[0])),
                         '../../swig'))
        import mapper
    except:
        print 'Error importing libmapper module.'
        sys.exit(1)

def main():
    if pwm.run_synth()==0:
        print "Error running synth."
        return

    try:
        window = Tkinter.Tk()
        window.title("libmapper PWM synth demo")

        gain = Tkinter.Scale(window, from_=0, to=100, label='gain',
                             orient=Tkinter.HORIZONTAL, length=300,
                             command=lambda n: pwm.set_gain(float(n)/100.0))
        gain.pack()

        freq = Tkinter.Scale(window, from_=0, to=1000, label='freq',
                             orient=Tkinter.HORIZONTAL, length=300,
                             command=lambda n: pwm.set_freq(float(n)))
        freq.pack()

        duty = Tkinter.Scale(window, from_=0, to=100, label='duty',
                             orient=Tkinter.HORIZONTAL, length=300,
                             command=lambda n: pwm.set_duty(float(n)/100.0))
        duty.pack()

        dev = mapper.device("tk_pwm", 9000)

        dev.add_input("/gain", 'f', lambda s, n: gain.set(n), None, 0, 100)
        dev.add_input("/freq", 'f', lambda s, n: freq.set(n), "Hz", 0, 1000)
        dev.add_input("/duty", 'f', lambda s, n: duty.set(n), None, 0, 100)

        def do_poll():
            dev.poll(0)
            window.after(5, do_poll)

        do_poll()
        window.mainloop()

    finally:
        pwm.stop_synth()

main()
