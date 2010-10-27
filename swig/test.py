
import sys, mapper

def h(sig, f):
    try:
        print sig.name, f
    except:
        print 'exception'
        print sig, f

def setup(d):
    sig = mapper.signal(1, "/freq", "i", "Hz", h)
    print 'inputs',d.num_inputs
    d.register_input(sig)
    print 'inputs',d.num_inputs
    print 'minimum',sig.minimum
    sig.minimum = 34.0
    print 'minimum',sig.minimum
    sig.minimum = 12
    print 'minimum',sig.minimum
    sig.minimum = None
    print 'minimum',sig.minimum
    print 'port',d.port
    print 'device name',d.name
    print 'signal name',sig.name
    print 'signal full name',sig.full_name
    while not d.ready():
        d.poll(10)
    print 'port',d.port
    print 'device name',d.name
    print 'signal name',sig.name
    print 'signal full name',sig.full_name
    print 'signal is_output',sig.is_output
    print 'signal length',sig.length
    print 'signal type', sig.type
    print 'signal is_output', sig.is_output
    print 'signal unit', sig.unit

dev = mapper.device("test", 9000)
setup(dev)

while not dev.ready():
    dev.poll(10)

for i in range(1000):
    dev.poll(10)
