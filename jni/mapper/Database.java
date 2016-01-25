
package mapper;

import mapper.database.*;
import mapper.device.*;
import mapper.map.*;
import mapper.signal.*;
import java.util.Set;

public class Database
{
    /* constructor */
    private native long mapperDatabaseNew(int subscribe_flags);
    public Database(Set<SubscriptionType> types) {
        int flags = 0;
        for (SubscriptionType t : SubscriptionType.values()) {
            if (types.contains(t))
                flags |= t.value();
        }
        _db = mapperDatabaseNew(flags);
        _dev_listener = null;
        _sig_listener = null;
        _map_listener = null;
    }
    public Database(SubscriptionType type) {
        _db = mapperDatabaseNew(type.value());
        _dev_listener = null;
        _sig_listener = null;
        _map_listener = null;
    }
    public Database() {
        _db = mapperDatabaseNew(SubscriptionType.ALL.value());
        _dev_listener = null;
        _sig_listener = null;
        _map_listener = null;
    }
    public Database(long db) {
        _db = db;
        _dev_listener = null;
        _sig_listener = null;
        _map_listener = null;
    }

    /* free */
    private native void mapperDatabaseFree(long db);
    public void free() {
        if (_db!=0)
            mapperDatabaseFree(_db);
        _db = 0;
    }

    /* network */
    private native long mapperDatabaseNetwork(long dbptr);
    public mapper.Network network() {
        long netptr = mapperDatabaseNetwork(_db);
        return new mapper.Network(netptr);
    }

    /* timeout */
    public native int timeout();
    public native Database setTimeout(int timeout);

    /* flush */
    public native Database flush();

    /* poll */
    public native Database poll(int timeout);
    public Database poll() { return poll(0); }

    /* subscriptions */
    private native void mapperDatabaseSubscribe(long db, mapper.Device dev,
                                                int flags, int timeout);
    public Database subscribe(mapper.Device dev, SubscriptionType type, int lease) {
        mapperDatabaseSubscribe(_db, dev, type.value(), lease);
        return this;
    }
    public Database subscribe(mapper.Device dev, Set<SubscriptionType> types,
                              int lease) {
        int flags = 0;
        for (SubscriptionType t : SubscriptionType.values()) {
            if (types.contains(t))
                flags |= t.value();
        }
        mapperDatabaseSubscribe(_db, dev, flags, lease);
        return this;
    }
    public native Database unsubscribe(mapper.Device dev);
    public native Database requestDevices();

    // Listeners
    private native void mapperDatabaseAddDeviceCB(long db,
                                                  mapper.database.DeviceListener l);
    public Database addDeviceListener(mapper.database.DeviceListener l) {
        if (l != _dev_listener)
            mapperDatabaseRemoveDeviceCB(_db, _dev_listener);
        mapperDatabaseAddDeviceCB(_db, l);
        _dev_listener = l;
        return this;
    }

    private native void mapperDatabaseRemoveDeviceCB(long db,
                                                     mapper.database.DeviceListener l);
    public Database removeDeviceListener(mapper.database.DeviceListener l) {
        if (l == _dev_listener) {
            mapperDatabaseRemoveDeviceCB(_db, l);
            _dev_listener = null;
        }
        return this;
    }

    private native void mapperDatabaseAddSignalCB(long db,
                                                  mapper.database.SignalListener l);
    public Database addSignalListener(mapper.database.SignalListener l) {
        if (l != _sig_listener)
            mapperDatabaseRemoveSignalCB(_db, _sig_listener);
        mapperDatabaseAddSignalCB(_db, l);
        _sig_listener = l;
        return this;
    }

    private native void mapperDatabaseRemoveSignalCB(long db,
                                                     mapper.database.SignalListener l);
    public Database removeSignalListener(mapper.database.SignalListener l) {
        if (l == _sig_listener) {
            mapperDatabaseRemoveSignalCB(_db, l);
            _sig_listener = null;
        }
        return this;
    }

    private native void mapperDatabaseAddMapCB(long db,
                                               mapper.database.MapListener l);
    public Database addMapListener(mapper.database.MapListener l)
    {
        if (l != _map_listener)
            mapperDatabaseRemoveMapCB(_db, _map_listener);
        mapperDatabaseAddMapCB(_db, l);
        _map_listener = l;
        return this;
    }

    private native void mapperDatabaseRemoveMapCB(long db,
                                                  mapper.database.MapListener l);
    public Database removeMapListener(mapper.database.MapListener l) {
        if (l == _map_listener) {
            mapperDatabaseRemoveMapCB(_db, l);
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

    private native long mapperDatabaseDevicesByProp(long db, String name,
                                                    Value value, int op);
    public mapper.device.Query devicesByProperty(String name, Value value,
                                                 mapper.database.Operator op) {
        return new mapper.device.Query(mapperDatabaseDevicesByProp(_db, name,
                                                                   value,
                                                                   op.value()));
    }

    // signals
    public native mapper.Signal signal(long id);

    private native long mapperDatabaseSignals(long db, String pattern,
                                              int direction);
    public mapper.signal.Query signals() {
        return new mapper.signal.Query(mapperDatabaseSignals(_db, null, 0));
    }
    public mapper.signal.Query signalsByNameMatch(String pattern) {
        return new mapper.signal.Query(mapperDatabaseSignals(_db, pattern, 0));
    }

    public mapper.signal.Query inputs() {
        return new mapper.signal.Query(mapperDatabaseSignals(_db, null,
                                                             Direction.INCOMING.value()));
    }
    public mapper.signal.Query inputsByNameMatch(String pattern) {
        return new mapper.signal.Query(mapperDatabaseSignals(_db, pattern,
                                                             Direction.INCOMING.value()));
    }

    public mapper.signal.Query outputs() {
        return new mapper.signal.Query(mapperDatabaseSignals(_db, null,
                                                             Direction.OUTGOING.value()));
    }
    public mapper.signal.Query outputsByNameMatch(String pattern) {
        return new mapper.signal.Query(mapperDatabaseSignals(_db, pattern,
                                                             Direction.OUTGOING.value()));
    }

    private native long mapperDatabaseSignalsByProp(long db, String name,
                                                    Value value, int op);
    public mapper.device.Query signalsByProperty(String name, Value value,
                                                 mapper.database.Operator op) {
        return new mapper.device.Query(mapperDatabaseSignalsByProp(_db, name,
                                                                   value,
                                                                   op.value()));
    }

    // maps
    public native mapper.Map map(long id);
    public native mapper.map.Query maps();
    private native long mapperDatabaseMapsByProp(long db, String name,
                                                 Value value, int op);
    public mapper.device.Query mapsByProperty(String name, Value value,
                                              mapper.database.Operator op) {
        return new mapper.device.Query(mapperDatabaseMapsByProp(_db, name, value,
                                                                op.value()));
    }
    private native long mapperDatabaseMapsBySlotProp(long db, String name,
                                                     Value value, int op);
    public mapper.device.Query mapsBySlotProperty(String name, Value value,
                                                  mapper.database.Operator op) {
        return new mapper.device.Query(mapperDatabaseMapsBySlotProp(_db, name,
                                                                    value,
                                                                    op.value()));
    }

    /* Note: this is _not_ guaranteed to run, the user should still call free()
     * explicitly when the database is no longer needed. */
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
