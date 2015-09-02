
package mapper;

import mapper.db.*;
import mapper.device.*;
import mapper.map.*;
import mapper.signal.*;
import java.util.Set;

public class Db
{
    /* constructor */
    private native long mapperDbNew(int subscribe_flags);
    public Db(Set<SubscriptionType> types) {
        int flags = 0;
        for (SubscriptionType t : SubscriptionType.values()) {
            if (types.contains(t))
                flags |= t.value();
        }
        _db = mapperDbNew(flags);
        _dev_listener = null;
        _sig_listener = null;
        _map_listener = null;
    }
    public Db(SubscriptionType type) {
        _db = mapperDbNew(type.value());
        _dev_listener = null;
        _sig_listener = null;
        _map_listener = null;
    }
    public Db() {
        _db = mapperDbNew(SubscriptionType.ALL.value());
        _dev_listener = null;
        _sig_listener = null;
        _map_listener = null;
    }
    public Db(long db) {
        _db = db;
        _dev_listener = null;
        _sig_listener = null;
        _map_listener = null;
    }

    /* free */
    private native void mapperDbFree(long db);
    public void free() {
        if (_db!=0)
            mapperDbFree(_db);
        _db = 0;
    }

    /* network */
    private native long mapperDbNetwork(long dbptr);
    public mapper.Network network() {
        long netptr = mapperDbNetwork(_db);
        return new mapper.Network(netptr);
    }

    /* timeout */
    public native int timeout();
    public native Db setTimeout(int timeout);

    /* flush */
    public native Db flush();

    /* update */
    public native Db update(int timeout);
    public Db update() { return update(0); }

    /* subscriptions */
    private native void mapperDbSubscribe(long db, mapper.Device dev, int flags,
                                          int timeout);
    public Db subscribe(mapper.Device dev, SubscriptionType type, int lease) {
        mapperDbSubscribe(_db, dev, type.value(), lease);
        return this;
    }
    public Db subscribe(mapper.Device dev, Set<SubscriptionType> types,
                        int lease) {
        int flags = 0;
        for (SubscriptionType t : SubscriptionType.values()) {
            if (types.contains(t))
                flags |= t.value();
        }
        mapperDbSubscribe(_db, dev, flags, lease);
        return this;
    }
    public native Db unsubscribe(mapper.Device dev);
    public native Db requestDevices();

    // Listeners
    private native void mapperDbAddDeviceCB(long db, mapper.db.DeviceListener l);
    public Db addDeviceListener(mapper.db.DeviceListener l) {
        if (l != _dev_listener)
            mapperDbRemoveDeviceCB(_db, _dev_listener);
        mapperDbAddDeviceCB(_db, l);
        _dev_listener = l;
        return this;
    }

    private native void mapperDbRemoveDeviceCB(long db,
                                               mapper.db.DeviceListener l);
    public Db removeDeviceListener(mapper.db.DeviceListener l) {
        if (l == _dev_listener) {
            mapperDbRemoveDeviceCB(_db, l);
            _dev_listener = null;
        }
        return this;
    }

    private native void mapperDbAddSignalCB(long db,
                                            mapper.db.SignalListener l);
    public Db addSignalListener(mapper.db.SignalListener l) {
        if (l != _sig_listener)
            mapperDbRemoveSignalCB(_db, _sig_listener);
        mapperDbAddSignalCB(_db, l);
        _sig_listener = l;
        return this;
    }

    private native void mapperDbRemoveSignalCB(long db,
                                               mapper.db.SignalListener l);
    public Db removeSignalListener(mapper.db.SignalListener l) {
        if (l == _sig_listener) {
            mapperDbRemoveSignalCB(_db, l);
            _sig_listener = null;
        }
        return this;
    }

    private native void mapperDbAddMapCB(long db, mapper.db.MapListener l);
    public Db addMapListener(mapper.db.MapListener l)
    {
        if (l != _map_listener)
            mapperDbRemoveMapCB(_db, _map_listener);
        mapperDbAddMapCB(_db, l);
        _map_listener = l;
        return this;
    }

    private native void mapperDbRemoveMapCB(long db, mapper.db.MapListener l);
    public Db removeMapListener(mapper.db.MapListener l) {
        if (l == _map_listener) {
            mapperDbRemoveMapCB(_db, l);
            _map_listener = null;
        }
        return this;
    }

    // devices
    public native mapper.Device device(String deviceName);
    public native mapper.Device device(long id);
    public native mapper.device.Query devices();
    public native mapper.device.Query localDevices();
    public native mapper.device.Query devicesByNameMatch(String pattern);

    private native long mapperDbDevicesByProp(long db, String name, Value value,
                                             int op);
    public mapper.device.Query devicesByProperty(String name, Value value,
                                                 mapper.db.Operator op) {
        return new mapper.device.Query(mapperDbDevicesByProp(_db, name, value,
                                                             op.value()));
    }

    // signals
    public native mapper.Signal signal(long id);

    private native long mapperDbSignals(long db, String pattern, int direction);
    public mapper.signal.Query signals() {
        return new mapper.signal.Query(mapperDbSignals(_db, null, 0));
    }
    public mapper.signal.Query signalsByNameMatch(String pattern) {
        return new mapper.signal.Query(mapperDbSignals(_db, pattern, 0));
    }

    public mapper.signal.Query inputs() {
        return new mapper.signal.Query(mapperDbSignals(_db, null,
                                                       Direction.INCOMING.value()));
    }
    public mapper.signal.Query inputsByNameMatch(String pattern) {
        return new mapper.signal.Query(mapperDbSignals(_db, pattern,
                                                       Direction.INCOMING.value()));
    }

    public mapper.signal.Query outputs() {
        return new mapper.signal.Query(mapperDbSignals(_db, null,
                                                       Direction.OUTGOING.value()));
    }
    public mapper.signal.Query outputsByNameMatch(String pattern) {
        return new mapper.signal.Query(mapperDbSignals(_db, pattern,
                                                       Direction.OUTGOING.value()));
    }

    private native long mapperDbSignalsByProp(long db, String name, Value value,
                                              int op);
    public mapper.device.Query signalsByProperty(String name, Value value,
                                                 mapper.db.Operator op) {
        return new mapper.device.Query(mapperDbSignalsByProp(_db, name, value,
                                                             op.value()));
    }

    // maps
    public native mapper.Map map(long id);
    public native mapper.map.Query maps();
    private native long mapperDbMapsByProp(long db, String name, Value value,
                                           int op);
    public mapper.device.Query mapsByProperty(String name, Value value,
                                              mapper.db.Operator op) {
        return new mapper.device.Query(mapperDbMapsByProp(_db, name, value,
                                                          op.value()));
    }
    private native long mapperDbMapsBySlotProp(long db, String name, Value value,
                                               int op);
    public mapper.device.Query mapsBySlotProperty(String name, Value value,
                                                  mapper.db.Operator op) {
        return new mapper.device.Query(mapperDbMapsBySlotProp(_db, name, value,
                                                              op.value()));
    }

    // Note: this is _not_ guaranteed to run, the user should still
    // call free() explicitly when the db is no longer needed.
    protected void finalize() throws Throwable {
        try {
            free();
        } finally {
            super.finalize();
        }
    }

    private long _db;
    // TODO: enable multiple listeners
    private DeviceListener _dev_listener;
    private SignalListener _sig_listener;
    private MapListener _map_listener;
    public boolean valid() {
        return _db != 0;
    }

    static {
        System.loadLibrary(NativeLib.name);
    }
}
