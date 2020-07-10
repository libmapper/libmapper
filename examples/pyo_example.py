#!/usr/bin/env python2

# Pyo is a sound synthesis system for Python.
# It is currently only available for Python 2

# Here we use it to demonstrate connecting a simple Pyo synthesizer to libmapper.

from pyo import *
import math
try:
    import mapper as mpr
except:
    try:
        # Try the "swig" directory, relative to the location of this
        # program, which is where it should be if the module has been
        # compiled but not installed.
        sys.path.append(
                        os.path.join(os.path.join(os.getcwd(),
                                                  os.path.dirname(sys.argv[0])),
                                     '../swig'))
        import mapper as mpr
    except:
        print('Error importing libmapper module.')
        sys.exit(1)

s = Server().boot()
s.start()

duty = SigTo(value=0.5, time=0.01, init=0.5, add=-0.5)
freq = SigTo(value=200, time=0.01, init=200)
amp = SigTo(value=0.5, time=0.01, init=0.0)

p = Phasor(freq=freq, add=Clip(duty, min=-0.5, max=0.5))
sig = DCBlock(Sig(value=Round(p), mul=[amp, amp])).out()

try:
    dev = mpr.device("pyo_pwm_example")
    dev.add_signal(mpr.DIR_IN, "frequency", 1, mpr.FLT, "Hz", 0, 1000, None, lambda s,e,i,v,t: freq.setValue(v))
    dev.add_signal(mpr.DIR_IN, "amplitude", 1, mpr.FLT, "normalized", 0, 1, None, lambda s,e,i,v,t: amp.setValue(v))
    dev.add_signal(mpr.DIR_IN, "duty", 1, 'f', "normalized", 0, 1, None, lambda s,e,i,v,t: duty.setValue(v))

    while True:
        dev.poll(5)

finally:
    s.stop()
    del dev
