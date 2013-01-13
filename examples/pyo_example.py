#!/usr/bin/env python

from pyo import *
import mapper
import math

s = Server().boot()
s.start()

duty = SigTo(value=0.5, time=0.5, init=0.5, add=-0.5)
freq = SigTo(value=200, time=0.5, init=200)
amp = SigTo(value=0.5, time=0.5, init=0.0)

p = Phasor(freq=freq, add=Clip(duty, min=-0.5, max=0.5))
sig = DCBlock(Sig(value=Round(p), mul=[amp, amp])).out()

try:
    dev = mapper.device("pyo_pwm_example", 9000)
    dev.add_input("/frequency", 1, 'f', "Hz", 0, 1000, lambda s,i,n,t: freq.setValue(n))
    dev.add_input("/amplitude", 1, 'f', "normalized", 0, 1, lambda s,i,n,t: amp.setValue(n))
    dev.add_input("/duty", 1, 'f', "normalized", 0, 1, lambda s,i,n,t: duty.setValue(n))

    while True:
        dev.poll(5)

finally:
    s.stop()