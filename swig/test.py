
import sys, mapper

def h(sig, id, f, timetag):
    try:
        print sig.name, f
    except:
        print 'exception'
        print sig, f

def setup(d):
    sig = d.add_input_signal("freq", 1, 'i', "Hz", None, None, h)

    while not d.ready():
        d.poll(10)

    print 'device name', d.name
    print 'device port', d.port
    print 'device ip', d.network().ip4
    print 'device interface', d.network().interface
    print 'device ordinal', d.ordinal

    d.set_properties({"testInt":5, "testFloat":12.7, "testString":["test","foo"],
                     "removed1":"shouldn't see this"})
    d.properties['testInt'] = 7
#    d.set_properties({"removed1":None, "removed2":"test"})
#    d.remove_property("removed1")

    print 'Printing', d.num_properties, 'properties:'
    for key, value in d.properties.items():
        print '  ', key, ':', value

    print 'device properties:', d.properties

    print 'signal name', sig.name
    print 'signal direction', sig.direction
    print 'signal length', sig.length
    print 'signal type', sig.type
    print 'signal direction', sig.direction
    print 'signal unit', sig.unit
    print 'signal minimum', sig.minimum
    sig.minimum = 34.0
    print 'signal minimum', sig.minimum
    sig.minimum = 12
    print 'signal minimum', sig.minimum
    sig.minimum = None
    print 'signal minimum', sig.minimum

    sig.properties['testInt'] = 3

    print 'signal properties:', sig.properties

    d.add_input_signal("insig", 4, 'f', None, None, None, h)
    d.add_output_signal("outsig", 4, 'f')
    print 'setup done!'

#check libmapper version
print 'using libmapper version', mapper.version
dev = mapper.device("test")
setup(dev)

def database_cb(rectype, record, action):
    print rectype,["ADDED", "MODIFIED", "REMOVED", "EXPIRED"][action],'callback'
    if rectype is 'device':
        print '  ', record.name

db = mapper.database(subscribe_flags=mapper.OBJ_ALL)

db.add_device_callback(lambda x,y:database_cb('device',x,y))
db.add_signal_callback(lambda x,y:database_cb('signal',x,y))
db.add_map_callback(lambda x,y:database_cb('map',x,y))

while not dev.ready():
    dev.poll(10)
    db.poll()

outsig = dev.signal("outsig")
for i in range(1000):
    dev.poll(10)
    db.poll()
    outsig.update([i+1,i+2,i+3,i+4])
    
    if i==250:
        map = mapper.map(outsig, dev.signal("insig"))
        map.mode = mapper.MODE_EXPRESSION
        map.expression = 'y=y{-1}+x'
        map.source().minimum = [1,2,3,4]
        map.source().bound_min = mapper.BOUND_WRAP
        map.source().bound_max = mapper.BOUND_CLAMP
        map.push()

#        # test creating multi-source map
#        map = mapper.map([sig1, sig2], sig3)
#        map.mode = mapper.MODE_EXPRESSION
#        map.expression = 'y=x0-x1'
#        map.push()

    if i==500:
        map.source().minimum = [10,11,12,13]
        map.muted = True
        map.mode = mapper.MODE_LINEAR
        map.push()

    if i==800:
        map.release()

print 'devices and signals:'
for i in db.devices():
    print "  ", i.name
    for j in i.signals():
        print "    ", j.name

print 'maps:'
for i in db.maps():
    print "    ", i.source().signal().device().name, ':', i.source().signal().name,\
        '->', i.destination().signal().device().name, ':', i.destination().signal().name

# combining queries
print 'signals matching \'out\' or \'req\':'
q1 = db.signals_by_name_match("out")
q1.join(db.signals_by_name_match("req"))
for i in q1:
    print "    ", i.name
