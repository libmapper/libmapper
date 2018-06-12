
package mapper;

import mapper.NativeLib;
import mapper.device.*;
import mapper.Property;
import mapper.signal.UpdateListener;
import mapper.signal.InstanceUpdateListener;
import mapper.Value;
import mapper.TimeTag;
import java.util.Iterator;

public class Device
{
    /* constructor */
    private native long mapperDeviceNew(String name, int port);
    public Device(String name) { _dev = mapperDeviceNew(name, 0); }
    public Device(String name, int port) { _dev = mapperDeviceNew(name, port); }
    public Device(long dev) { _dev = dev; }

    /* free */
    private native void mapperDeviceFree(long _d);
    public void free() {
        if (_dev != 0)
            mapperDeviceFree(_dev);
        _dev = 0;
    }

    /* poll */
    private native int mapperDevicePoll(long _d, int timeout);
    public int poll(int timeout) { return mapperDevicePoll(_dev, timeout); }
    public int poll() { return mapperDevicePoll(_dev, 0); }

    /* add signals */
    private native Signal mapperAddSignal(int dir, int numInstances, String name,
                                          int length, char type, String unit,
                                          Value minimum, Value maximum,
                                          mapper.signal.UpdateListener l);
    public Signal addSignal(Direction dir, int numInstances, String name,
                            int length, char type, String unit,
                            Value minimum, Value maximum,
                            mapper.signal.UpdateListener l) {
        return mapperAddSignal(dir.value(), numInstances, name, length, type,
                               unit, minimum, maximum, l);
    }
    public Signal addInputSignal(String name, int length, char type,
                                 String unit, Value minimum, Value maximum,
                                 mapper.signal.UpdateListener l) {
        return mapperAddSignal(Direction.INCOMING.value(), 1, name, length,
                               type, unit, minimum, maximum, l);
    }

    public Signal addOutputSignal(String name, int length, char type,
                                  String unit, Value minimum, Value maximum) {
        return mapperAddSignal(Direction.OUTGOING.value(), 1, name, length,
                               type, unit, minimum, maximum, null);
    }

    /* remove signals */
    private native void mapperDeviceRemoveSignal(long _d, Signal sig);
    public Device removeSignal(Signal sig) {
        mapperDeviceRemoveSignal(_dev, sig);
        return this;
    }

    /* signal groups */
    public native long addSignalGroup();
    public native Device removeSignalGroup(long group);

    /* retrieve Network object */
    private native long mapperDeviceNetwork(long dev);
    public mapper.Network network() {
        return new Network(mapperDeviceNetwork(_dev));
    }

    /* properties */
    public native int numProperties();
    public native Value property(String name);
    public native Property property(int index);
    public native Device setProperty(String name, Value value, boolean publish);
    public Device setProperty(String name, Value value) {
        return setProperty(name, value, true);
    }
    public Device setProperty(Property prop) {
        return setProperty(prop.name, prop.value, prop.publish);
    }
    public native Device removeProperty(String name);
    public Device removeProperty(Property prop) {
        return removeProperty(prop.name);
    }

    /* clear staged properties */
    public native Device clearStagedProperties();

    /* push */
    public native Device push();

    /* property: host */
    public native String host();

    /* property: id */
    public native long id();

    /* property: is_local */
    public native boolean isLocal();

    /* property: name */
    public native String name();

    /* property: num_signals */
    public native int numSignals(int direction);
    public int numSignals() { return numSignals(0); }
    public int numInputs() {
        return numSignals(Direction.INCOMING.value());
    }
    public int numOutputs() {
        return numSignals(Direction.OUTGOING.value());
    }

    /* property: num_maps */
    public native int numMaps(int direction);
    public int numMaps() { return numMaps(0); }

    /* property: num_links */
    public native int numLinks(int direction);
    public int numLinks() { return numLinks(0); }

    /* property: ordinal */
    public native int ordinal();

    /* property: port */
    public native int port();

    /* property: ready */
    public native boolean ready();

    /* property: synced */
    public native TimeTag synced();

    /* property: version */
    public native int version();

    /* manage update queues */
    public native TimeTag startQueue(TimeTag tt);
    public TimeTag startQueue() {
        return startQueue(null);
    }
    public native Device sendQueue(TimeTag tt);

    // listeners
    private native void mapperDeviceSetLinkCB(long dev,
                                              mapper.device.LinkListener l);
    public Device setLinkListener(mapper.device.LinkListener l) {
        mapperDeviceSetLinkCB(_dev, l);
        return this;
    }
    private native void mapperDeviceSetMapCB(long dev,
                                             mapper.device.MapListener l);
    public Device setMapListener(mapper.device.MapListener l) {
        mapperDeviceSetMapCB(_dev, l);
        return this;
    }

    /* retrieve associated signals */
    public native Signal signal(long id);
    public native Signal signal(String name);

    public native mapper.signal.Query signals(int direction);
    public mapper.signal.Query signals() { return signals(0); }
    public mapper.signal.Query inputs() {
        return signals(Direction.INCOMING.value());
    }
    public mapper.signal.Query outputs() {
        return signals(Direction.OUTGOING.value());
    }

    /* retrieve associated maps */
    private native long mapperDeviceMaps(long dev, int direction);
    public mapper.map.Query maps(Direction direction) {
        return new mapper.map.Query(mapperDeviceMaps(_dev, direction.value()));
    }
    public mapper.map.Query maps() {
        return maps(Direction.ANY);
    }
    public mapper.map.Query incomingMaps() {
        return maps(Direction.INCOMING);
    }
    public mapper.map.Query outgoingMaps() {
        return maps(Direction.OUTGOING);
    }

    // Note: this is _not_ guaranteed to run, the user should still
    // call free() explicitly when the device is no longer needed.
    protected void finalize() throws Throwable {
        try {
            free();
        } finally {
            super.finalize();
        }
    }

    private long _dev;
    public boolean valid() {
        return _dev != 0;
    }

    static {
        System.loadLibrary(NativeLib.name);
    }
}
