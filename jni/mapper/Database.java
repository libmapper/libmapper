
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
    public Database(Set<ObjectType> types) {
        int flags = 0;
        for (ObjectType t : ObjectType.values()) {
            if (types.contains(t))
                flags |= t.value();
        }
        _db = mapperDatabaseNew(flags);
        _dev_listener = null;
        _link_listener = null;
        _sig_listener = null;
        _map_listener = null;
    }
    public Database(ObjectType type) {
        _db = mapperDatabaseNew(type.value());
        _dev_listener = null;
        _link_listener = null;
        _sig_listener = null;
        _map_listener = null;
    }
    public Database() {
        _db = mapperDatabaseNew(ObjectType.ALL.value());
        _dev_listener = null;
        _link_listener = null;
        _sig_listener = null;
        _map_listener = null;
    }
    public Database(long db) {
        _db = db;
        _dev_listener = null;
        _link_listener = null;
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
    public native Database flush(int timeout, boolean quiet);
    public Database flush() {return flush(0, false); }
    public Database flush(int timeout) {return flush(timeout, false); }

    /* poll */
    public native Database poll(int timeout);
    public Database poll() { return poll(0); }

    /* subscriptions */
    private native void mapperDatabaseSubscribe(long db, mapper.Device dev,
                                                int flags, int timeout);
    public Database subscribe(mapper.Device dev, ObjectType type, int lease) {
        mapperDatabaseSubscribe(_db, dev, type.value(), lease);
        return this;
    }
    public Database subscribe(mapper.Device dev, ObjectType type) {
        return subscribe(dev, type, -1);
    }
    public Database subscribe(mapper.Device dev, Set<ObjectType> types,
                              int lease) {
        int flags = 0;
        for (ObjectType t : ObjectType.values()) {
            if (types.contains(t))
                flags |= t.value();
        }
        mapperDatabaseSubscribe(_db, dev, flags, lease);
        return this;
    }
    public Database subscribe(mapper.Device dev, Set<ObjectType> types) {
        return subscribe(dev, types, -1);
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

    private native void mapperDatabaseAddLinkCB(long db,
                                                mapper.database.LinkListener l);
    public Database addLinkListener(mapper.database.LinkListener l) {
        if (l != _link_listener)
            mapperDatabaseRemoveLinkCB(_db, _link_listener);
        mapperDatabaseAddLinkCB(_db, l);
        _link_listener = l;
        return this;
    }

    private native void mapperDatabaseRemoveLinkCB(long db,
                                                   mapper.database.LinkListener l);
    public Database removeLinkListener(mapper.database.LinkListener l) {
        if (l == _link_listener) {
            mapperDatabaseRemoveLinkCB(_db, l);
            _link_listener = null;
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
    public native int numDevices();
    public native mapper.Device device(String deviceName);
    public native mapper.Device device(long id);
    public native mapper.device.Query devices();
    public native mapper.device.Query devices(String name);

    private native long mapperDatabaseDevicesByProp(long db, String name,
                                                    Value value, int op);
    public mapper.device.Query devices(String name, Value value,
                                       mapper.database.Operator op) {
        return new mapper.device.Query(mapperDatabaseDevicesByProp(_db, name,
                                                                   value,
                                                                   op.value()));
    }

    // links
    public native int numLinks();
    public native mapper.Link link(long id);
    public native mapper.link.Query links();
    private native long mapperDatabaseLinksByProp(long db, String name,
                                                  Value value, int op);
    public mapper.link.Query linksByProperty(String name, Value value,
                                             mapper.database.Operator op) {
        return new mapper.link.Query(mapperDatabaseLinksByProp(_db, name, value,
                                                               op.value()));
    }

    // signals
    public native int numSignals(int dir);
    public native mapper.Signal signal(long id);

    private native long mapperDatabaseSignals(long db, String name, int dir);
    public mapper.signal.Query signals() {
        return new mapper.signal.Query(mapperDatabaseSignals(_db, null, 0));
    }
    public mapper.signal.Query signals(String name) {
        return new mapper.signal.Query(mapperDatabaseSignals(_db, name, 0));
    }
    public mapper.signal.Query signals(Direction dir) {
        return new mapper.signal.Query(mapperDatabaseSignals(_db, null,
                                                             dir.value()));
    }
    public mapper.signal.Query signals(String name, Direction dir) {
        return new mapper.signal.Query(mapperDatabaseSignals(_db, name,
                                                             dir.value()));
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
    public native int numMaps();
    public native mapper.Map map(long id);
    public native mapper.map.Query maps();
    private native long mapperDatabaseMapsByProp(long db, String name,
                                                 Value value, int op);
    public mapper.map.Query mapsByProperty(String name, Value value,
                                           mapper.database.Operator op) {
        return new mapper.map.Query(mapperDatabaseMapsByProp(_db, name, value,
                                                             op.value()));
    }
    private native long mapperDatabaseMapsBySlotProp(long db, String name,
                                                     Value value, int op);
    public mapper.map.Query mapsBySlotProperty(String name, Value value,
                                               mapper.database.Operator op) {
        return new mapper.map.Query(mapperDatabaseMapsBySlotProp(_db, name,
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
    private mapper.database.MapListener _map_listener;
    private mapper.database.LinkListener _link_listener;
    public boolean valid() {
        return _db != 0;
    }

    static {
        System.loadLibrary(NativeLib.name);
    }
}
