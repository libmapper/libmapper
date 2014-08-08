
import sys, mapper

def h(sig, id, f, timetag):
    try:
        print sig.name, f
    except:
        print 'exception'
        print sig, f

def setup(d):
    sig = d.add_input("/freq", 1, 'i', "Hz", None, None, h)
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
    print 'device port',d.port
    print 'device ip',d.ip4
    print 'device interface',d.interface
    print 'device ordinal',d.ordinal
    print 'signal name',sig.name
    print 'signal full name',sig.full_name
    while not d.ready():
        d.poll(10)
    print 'port',d.port
    print 'device name',d.name
    print 'device ip',d.ip4
    print 'device interface',d.interface
    print 'device ordinal',d.ordinal
    print 'signal name',sig.name
    print 'signal full name',sig.full_name
    print 'signal is_output',sig.is_output
    print 'signal length',sig.length
    print 'signal type', sig.type
    print 'signal is_output', sig.is_output
    print 'signal unit', sig.unit
    d.set_properties({"testInt":5, "testFloat":12.7, "testString":"test",
                     "removed1":"shouldn't see this"})
    d.properties['testInt'] = 7
    d.set_properties({"removed1":None, "removed2":"test"})
    d.remove_property("removed2")
    print 'device properties', d.properties
    print 'signal properties:', sig.properties
    sig.properties['testInt'] = 3
    print 'signal properties:', sig.properties
    d.add_input("/insig", 4, 'f', None, None, None, h)
    d.add_output("/outsig", 4, 'f')
    print 'setup done!'

dev = mapper.device("test")
setup(dev)

def db_cb(rectype, record, action):
    print rectype,["MODIFY","NEW","REMOVE"][action],'callback -'
    print '  record:',record

mon = mapper.monitor(autosubscribe_flags=mapper.SUB_DEVICE)

mon.db.add_device_callback(lambda x,y:db_cb('device',x,y))
mon.db.add_signal_callback(lambda x,y:db_cb('signal',x,y))
mon.db.add_connection_callback(lambda x,y:db_cb('connection',x,y))
l = lambda x,y:db_cb('link',x,y)
mon.db.add_link_callback(l)
mon.db.remove_link_callback(l)

while not dev.ready():
    dev.poll(10)
    mon.poll()

for i in range(1000):
    dev.poll(10)
    mon.poll()
    if i==250:
        mon.link("/test.1", "/test.1")
    if i==500:
        mon.connect("/test.1/outsig", "/test.1/insig",
                    {'mode': mapper.MO_EXPRESSION,
                     'expression': 'y=x',
                     'src_min': [1,2,3,4],
                     'bound_min': mapper.BA_WRAP,
                     'bound_max': mapper.BA_CLAMP})
    if i==750:
        mon.modify_connection("/test.1/outsig", "/test.1/insig",
                              {'src_min':[10,11,12,13],
                               'muted':True,
                               'mode': mapper.MO_LINEAR})

for i in [('devices', mon.db.devices),
          ('inputs', mon.db.inputs),
          ('outputs', mon.db.outputs),
          ('connections', mon.db.connections),
          ('links', mon.db.links)]:
    print i[0],':'
    for j in i[1]():
        print "  {"
        for k in j:
            print "    ", k, ":", j[k]
        print "  }"

print 'devices matching "send":'
for i in mon.db.devices('send'):
    print i
print 'outputs for device "/test.1" matching "3":'
for i in mon.db.match_outputs('/test.1', '3'):
    print i
print 'links for device "/test.1":'
for i in mon.db.links_by_src('/test.1'):
    print i
print 'link for /test.1, /test.1:'
print mon.db.link("/test.1", "/test.1")
print 'not found link:'
print mon.db.link("/foo", "/bar")
