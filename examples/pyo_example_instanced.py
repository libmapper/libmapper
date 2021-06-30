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

numInst = 10
freq = [SigTo(value=200, time=0.05, init=200) for i in range(numInst)]
amp = [SigTo(value=0.0, time=0.05, init=0.0) for i in range(numInst)]
duty = [SigTo(value=0.5, time=0.05, init=0.5, add=-0.5) for i in range(numInst)]

mm = Mixer(outs=2, chnls=numInst, time=.025).out()
for i in range(numInst):
    p = Phasor(freq=freq[i], add=Clip(duty[i], min=-0.5, max=0.5))
    sig = DCBlock(Sig(value=Round(p), mul=[amp[i], amp[i]]))
    mm.addInput(i, sig)
    mm.setAmp(i,0,1/numInst)
    mm.setAmp(i,1,1/numInst)

freqSig = None
ampSig = None
dutySig = None

def release_instance(i):
    amp[i].setValue(0)
    freqSig.release_instance(i)
    ampSig.release_instance(i)
    dutySig.release_instance(i)

def freqHandler(s, e, i, v, t):
    if e == mpr.SIG_UPDATE:
        freq[i].setValue(v)
    elif e == mpr.SIG_REL_UPSTRM or e == mpr.SIG_INST_OFLW:
        release_instance(i)

def ampHandler(s, e, i, v, t):
    if e == mpr.SIG_UPDATE:
        amp[i].setValue(v)
    elif e == mpr.SIG_REL_UPSTRM or e == mpr.SIG_INST_OFLW:
        release_instance(i)

def dutyHandler(s, e, i, v, t):
    if e == mpr.SIG_UPDATE:
        duty[i].setValue(v)
    elif e == mpr.SIG_REL_UPSTRM or e == mpr.SIG_INST_OFLW:
        release_instance(i)

try:
    dev = mpr.device("pyo_pwm_example")
    freqSig = dev.add_signal(mpr.DIR_IN, "frequency", 1, mpr.FLT, "Hz", 0, 1000, numInst, freqHandler)
    ampSig = dev.add_signal(mpr.DIR_IN, "amplitude", 1, mpr.FLT, "normalized", 0, 1, numInst, ampHandler)
    dutySig = dev.add_signal(mpr.DIR_IN, "duty", 1, mpr.FLT, "normalized", 0, 1, numInst, dutyHandler)

    while True:
        dev.poll(5)

finally:
    s.stop()
    del dev
