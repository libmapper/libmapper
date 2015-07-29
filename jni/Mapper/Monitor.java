
package Mapper;

import Mapper.*;
import Mapper.Db.*;

public class Monitor
{
    /*! Bit flags for coordinating monitor metadata subscriptions. */
    public static final int SUBSCRIBE_NONE              = 0x00;
    public static final int SUBSCRIBE_DEVICE            = 0x01;
    public static final int SUBSCRIBE_DEVICE_INPUTS     = 0x02;
    public static final int SUBSCRIBE_DEVICE_OUTPUTS    = 0x04;
    public static final int SUBSCRIBE_DEVICE_SIGNALS    = 0x06;
    public static final int SUBSCRIBE_DEVICE_MAPS_IN    = 0x10;
    public static final int SUBSCRIBE_DEVICE_MAPS_OUT   = 0x20;
    public static final int SUBSCRIBE_DEVICE_MAPS       = 0x30;
    public static final int SUBSCRIBE_ALL               = 0xFF;

    public Monitor(int subscribeFlags) {
        _monitor = mmon_new(subscribeFlags);
        Db = new Db(mmon_get_db(_monitor));
    }
    public Monitor() {
        _monitor = mmon_new(0);
        Db = new Db(mmon_get_db(_monitor));
    }

    public void free() {
        if (_monitor!=0)
            mmon_free(_monitor);
        _monitor = 0;
    }

    public int poll(int timeout) {
        int foo = mmon_poll(_monitor, timeout);
        return foo;
    }
    public int poll() {
        return this.poll(0);
    }

    public class Db {

        /*! The set of possible actions on a db entity. */
        public static final int MODIFIED = 0;
        public static final int NEW      = 1;
        public static final int REMOVED  = 2;

        private Db(long d) {
            _db = d;
            _device_cb = null;
            _signal_cb = null;
            _map_cb = null;
        }

        private void checkMonitor() {
            if (_monitor._monitor == 0)
                throw new NullPointerException(
                    "Db object associated with invalid Monitor");
        }

        public boolean valid() {
            return _monitor._monitor != 0;
        }

        // Callbacks
        public void addDeviceCallback(Mapper.Db.DeviceListener handler)
        {
            if (handler != _device_cb)
                mdb_remove_device_callback(_db, _device_cb);
            mdb_add_device_callback(_db, handler);
            _device_cb = handler;
        }
        private native void mdb_add_device_callback(long db, Mapper.Db.DeviceListener handler);

        public void removeDeviceCallback(Mapper.Db.DeviceListener handler)
        {
            mdb_remove_device_callback(_db, handler);
        }
        private native void mdb_remove_device_callback(long db, Mapper.Db.DeviceListener handler);

        public void addSignalCallback(Mapper.Db.SignalListener handler)
        {
            if (handler != _signal_cb)
                mdb_remove_signal_callback(_db, _signal_cb);
            mdb_add_signal_callback(_db, handler);
            _signal_cb = handler;
        }
        private native void mdb_add_signal_callback(long db, Mapper.Db.SignalListener handler);

        public void removeSignalCallback(Mapper.Db.SignalListener handler)
        {
            mdb_remove_signal_callback(_db, handler);
        }
        private native void mdb_remove_signal_callback(long db, Mapper.Db.SignalListener handler);

        public void addMapCallback(Mapper.Db.MapListener handler)
        {
            if (handler != _map_cb)
                mdb_remove_map_callback(_db, _map_cb);
            mdb_add_map_callback(_db, handler);
            _map_cb = handler;
        }
        private native void mdb_add_map_callback(long db, Mapper.Db.MapListener handler);

        public void removeMapCallback(Mapper.Db.MapListener handler)
        {
            mdb_remove_map_callback(_db, handler);
        }
        private native void mdb_remove_map_callback(long db, Mapper.Db.MapListener handler);

        // Db.Device
        public native Mapper.Db.DeviceCollection devices();
        public native Mapper.Db.Device device(String deviceName);
        public native Mapper.Db.Device device(long id);
        public native Mapper.Db.DeviceCollection devicesByNameMatch(String pattern);

        // Db.Signal
        public native Mapper.Db.Signal signal(long id);
        private native long mdb_signals(long db, String pattern, int direction);
        public Mapper.Db.SignalCollection signals()
        {
            long _s = mdb_signals(_db, null, 0);
            return new Mapper.Db.SignalCollection(_s);
        }
        public Mapper.Db.SignalCollection signalsByNameMatch(String pattern)
        {
            long _s = mdb_signals(_db, pattern, 0);
            return new Mapper.Db.SignalCollection(_s);
        }

        // Db.Input
        public Mapper.Db.SignalCollection inputs()
        {
            long _s = mdb_signals(_db, null, 1);
            return new Mapper.Db.SignalCollection(_s);
        }
        public Mapper.Db.SignalCollection inputsByNameMatch(String pattern)
        {
            long _s = mdb_signals(_db, pattern, 1);
            return new Mapper.Db.SignalCollection(_s);
        }

        // Db.Output
        public Mapper.Db.SignalCollection outputs()
        {
            long _s = mdb_signals(_db, null, 2);
            return new Mapper.Db.SignalCollection(_s);
        }
        public Mapper.Db.SignalCollection outputsByNameMatch(String pattern)
        {
            long _s = mdb_signals(_db, pattern, 2);
            return new Mapper.Db.SignalCollection(_s);
        }

        // Db.Map
        public native Mapper.Db.Map map(long id);
        private native long mdb_maps(long db);
        public Mapper.Db.MapCollection maps()
        {
            long _m = mdb_maps(_db);
            return new Mapper.Db.MapCollection(_m);
        }

        private long _db;
        private Monitor _monitor;

        // TODO: enable multiple listeners
        private Mapper.Db.DeviceListener _device_cb;
        private Mapper.Db.SignalListener _signal_cb;
        private Mapper.Db.MapListener _map_cb;
    };

    public void subscribe(String deviceName, int subscribeFlags, int timeout)
    {
        mmon_subscribe(_monitor, deviceName, subscribeFlags, timeout);
    }

    public void unsubscribe(String deviceName)
    {
        mmon_unsubscribe(_monitor, deviceName);
    }

    public void map(Mapper.Device.Signal source, Mapper.Device.Signal dest)
    {
        Mapper.Device.Signal[] sources = {source};
        mmon_map_or_mod_sigs(_monitor, sources, dest, null, 0);
    }
    public void map(Mapper.Device.Signal[] sources, Mapper.Device.Signal dest)
    {
        mmon_map_or_mod_sigs(_monitor, sources, dest, null, 0);
    }
    public void map(Mapper.Device.Signal source, Mapper.Device.Signal dest,
                    Mapper.Db.Map props)
    {
        Mapper.Device.Signal[] sources = {source};
        mmon_map_or_mod_sigs(_monitor, sources, dest, props, 0);
    }
    public void map(Mapper.Device.Signal[] sources, Mapper.Device.Signal dest,
                    Mapper.Db.Map props)
    {
        mmon_map_or_mod_sigs(_monitor, sources, dest, props, 0);
    }
    public void map(Mapper.Db.Signal source, Mapper.Db.Signal dest)
    {
        Mapper.Db.Signal[] sources = {source};
        mmon_map_or_mod_db_sigs(_monitor, sources, dest, null, 0);
    }
    public void map(Mapper.Db.Signal[] sources, Mapper.Db.Signal dest)
    {
        mmon_map_or_mod_db_sigs(_monitor, sources, dest, null, 0);
    }
    public void map(Mapper.Db.Signal source, Mapper.Db.Signal dest,
                    Mapper.Db.Map props)
    {
        Mapper.Db.Signal[] sources = {source};
        mmon_map_or_mod_db_sigs(_monitor, sources, dest, props, 0);
    }
    public void map(Mapper.Db.Signal[] sources, Mapper.Db.Signal dest,
                    Mapper.Db.Map props)
    {
        mmon_map_or_mod_db_sigs(_monitor, sources, dest, props, 0);
    }

    public void unmap(Mapper.Db.Map map)
    {
        mmon_unmap(_monitor, map);
    }
    public void unmap(Mapper.Device.Signal source, Mapper.Device.Signal dest)
    {
        Mapper.Device.Signal[] sources = {source};
        mmon_unmap_sigs(_monitor, sources, dest);
    }
    public void unmap(Mapper.Device.Signal[] sources, Mapper.Device.Signal dest)
    {
        mmon_unmap_sigs(_monitor, sources, dest);
    }
    public void unmap(Mapper.Db.Signal source, Mapper.Db.Signal dest)
    {
        Mapper.Db.Signal[] sources = {source};
        mmon_unmap_db_sigs(_monitor, sources, dest);
    }
    public void unmap(Mapper.Db.Signal[] sources, Mapper.Db.Signal dest)
    {
        mmon_unmap_db_sigs(_monitor, sources, dest);
    }

    public TimeTag now()
    {
        return mmon_now(_monitor);
    }

    // Note: this is _not_ guaranteed to run, the user should still
    // call free() explicitly when the monitor is no longer needed.
    protected void finalize() throws Throwable {
        try {
            free();
        } finally {
            super.finalize();
        }
    }

    private native long mmon_new(int subscribe_flags);
    private native long mmon_get_db(long mon);
    private native void mmon_free(long mon);
    private native int mmon_poll(long mon, int timeout);
    private native void mmon_subscribe(long mon, String device_name,
                                       int subscribe_flags, int timeout);
    private native void mmon_unsubscribe(long mon, String device_name);
    private native void mmon_map_or_mod_sigs(long mon,
                                             Mapper.Device.Signal[] sources,
                                             Mapper.Device.Signal dest,
                                             Mapper.Db.Map props,
                                             int modify);
    private native void mmon_map_or_mod_db_sigs(long mon,
                                                Mapper.Db.Signal[] sources,
                                                Mapper.Db.Signal dest,
                                                Mapper.Db.Map props,
                                                int modify);
//    private native void mmon_modify_map(long mon, Mapper.Db.Map map);
    private native void mmon_unmap(long mon, Mapper.Db.Map map);
    private native void mmon_unmap_sigs(long mon, Mapper.Device.Signal[] sources,
                                        Mapper.Device.Signal dest);
    private native void mmon_unmap_db_sigs(long mon, Mapper.Db.Signal[] sources,
                                           Mapper.Db.Signal dest);
    private native TimeTag mmon_now(long mon);

    private long _monitor;
    public Mapper.Monitor.Db Db;
    public boolean valid() {
        return _monitor != 0;
    }

    static {
        System.loadLibrary(NativeLib.name);
    }
}
