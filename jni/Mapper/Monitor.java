
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

    public Monitor(int autosubscribeFlags) {
        _monitor = mmon_new(autosubscribeFlags);
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
        public native Mapper.Db.Device get_device(String deviceName);
        public native Mapper.Db.DeviceCollection match_devices(String pattern);

        // Db.Input
        public native Mapper.Db.Signal getInput(String deviceName,
                                                String signalName);
        private native long mdb_inputs(long db, String deviceName);
        public Mapper.Db.SignalCollection inputs()
        {
            long _s = mdb_inputs(_db, null);
            return new Mapper.Db.SignalCollection(_s);
        }
        public Mapper.Db.SignalCollection inputs(String deviceName)
        {
            long _s = mdb_inputs(_db, deviceName);
            return new Mapper.Db.SignalCollection(_s);
        }
        public native Mapper.Db.SignalCollection matchInputs(String deviceName,
                                                             String signalPattern);

        // Db.Output
        public native Mapper.Db.Signal getOutput(String deviceName,
                                                 String signalName);
        private native long mdb_outputs(long db, String deviceName);
        public Mapper.Db.SignalCollection outputs()
        {
            long _s = mdb_outputs(_db, null);
            return new Mapper.Db.SignalCollection(_s);
        }
        public Mapper.Db.SignalCollection outputs(String deviceName)
        {
            long _s = mdb_outputs(_db, deviceName);
            return new Mapper.Db.SignalCollection(_s);
        }
        public native Mapper.Db.SignalCollection matchOutputs(String deviceName,
                                                              String signalPattern);

        // Db.Map
        private native long mdb_maps(long db, String deviceName);
        public Mapper.Db.MapCollection maps()
        {
            long _c = mdb_maps(_db, null);
            return new Mapper.Db.MapCollection(_c);
        }
        public Mapper.Db.MapCollection maps(String deviceName)
        {
            long _c = mdb_maps(_db, deviceName);
            return new Mapper.Db.MapCollection(_c);
        }
        public Mapper.Db.MapCollection maps(Mapper.Device device)
        {
            long _c = mdb_maps(_db, device.name());
            return new Mapper.Db.MapCollection(_c);
        }
        public Mapper.Db.MapCollection maps(Mapper.Db.Device device)
        {
            long _c = mdb_maps(_db, device.name());
            return new Mapper.Db.MapCollection(_c);
        }

        public native Mapper.Db.MapCollection maps(Mapper.Db.SignalCollection src,
                                                   Mapper.Db.SignalCollection dest);
        public native Mapper.Db.MapCollection maps(String[] srcDevice,
                                                   String destDevice);
        public Mapper.Db.MapCollection maps(String source, String dest)
        {
            String[] sources = {source};
            return this.maps(sources, dest);
        }
        public Mapper.Db.MapCollection maps(Mapper.Device[] sources,
                                            Mapper.Device dest)
        {
            int length = sources.length;
            String[] srcNames = new String[length];
            for (int i = 0; i < length; i++)
                srcNames[i] = sources[i].name();
            return this.maps(srcNames, dest.name());
        }
        public Mapper.Db.MapCollection maps(Mapper.Device source,
                                            Mapper.Device dest)
        {
            Mapper.Device[] sources = {source};
            return this.maps(sources, dest);
        }
        public Mapper.Db.MapCollection maps(Mapper.Db.Device[] sources,
                                            Mapper.Db.Device dest)
        {
            int length = sources.length;
            String[] srcNames = new String[length];
            for (int i = 0; i < length; i++)
                srcNames[i] = sources[i].name();
            return this.maps(srcNames, dest.name());
        }
        public Mapper.Db.MapCollection maps(Mapper.Db.Device source,
                                            Mapper.Db.Device dest)
        {
            Mapper.Db.Device[] sources = {source};
            return this.maps(sources, dest);
        }

        private native Mapper.Db.MapCollection mdb_maps_by_src(
            long db, String deviceName, String signalName);
        public Mapper.Db.MapCollection mapsBySrc(String signalName)
            { return mdb_maps_by_src(_db, null, signalName); }
        public Mapper.Db.MapCollection mapsBySrc(String deviceName,
                                                 String signalName)
            { return mdb_maps_by_src(_db, deviceName, signalName); }
        public Mapper.Db.MapCollection mapsBySrc(Mapper.Device.Signal signal)
            { return mdb_maps_by_src(_db, null, signal.fullName()); }
        public Mapper.Db.MapCollection mapsBySrc(Mapper.Db.Signal signal)
            { return mdb_maps_by_src(_db, null, signal.fullName()); }

        private native Mapper.Db.MapCollection mdb_maps_by_dest(
            long db, String deviceName, String signalName);
        public Mapper.Db.MapCollection mapsByDest(String signalName)
            { return mdb_maps_by_dest(_db, null, signalName); }
        public Mapper.Db.MapCollection mapsByDest(String deviceName,
                                                  String signalName)
            { return mdb_maps_by_dest(_db, deviceName, signalName); }
        public Mapper.Db.MapCollection mapsByDest(Mapper.Device.Signal signal)
            { return mdb_maps_by_dest(_db, null, signal.fullName()); }
        public Mapper.Db.MapCollection mapsByDest(Mapper.Db.Signal signal)
            { return mdb_maps_by_dest(_db, null, signal.fullName()); }

        private native long map_by_hash(long db, int hash);
        public Mapper.Db.Map map(int hash)
            { return new Mapper.Db.Map(map_by_hash(_db, hash)); }
        private native long map_by_names(long db, String[] srcNames,
                                         String destName);
        public Mapper.Db.Map map(String[] srcNames, String destName)
        {
            return new Mapper.Db.Map(map_by_names(_db, srcNames, destName));
        }
        public Mapper.Db.Map map(Mapper.Device.Signal[] sources,
                                 Mapper.Device.Signal dest)
        {
            int length = sources.length;
            String[] srcNames = new String[length];
            for (int i = 0; i < length; i++)
                srcNames[i] = sources[i].fullName();
            return new Mapper.Db.Map(map_by_names(_db, srcNames, dest.fullName()));
        }
        public Mapper.Db.Map map(Mapper.Db.Signal[] sources, Mapper.Db.Signal dest)
        {
            int length = sources.length;
            String[] srcNames = new String[length];
            for (int i = 0; i < length; i++)
                srcNames[i] = sources[i].fullName();
            return new Mapper.Db.Map(map_by_names(_db, srcNames, dest.fullName()));
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

    public void modifyMap(Mapper.Device.Signal source, Mapper.Device.Signal dest,
                          Mapper.Db.Map props)
    {
        Mapper.Device.Signal[] sources = {source};
        mmon_map_or_mod_sigs(_monitor, sources, dest, props, 1);
    }
    public void modifyMap(Mapper.Device.Signal[] sources,
                          Mapper.Device.Signal dest,
                          Mapper.Db.Map props)
    {
        mmon_map_or_mod_sigs(_monitor, sources, dest, props, 1);
    }
    public void modifyMap(Mapper.Db.Signal source, Mapper.Db.Signal dest,
                          Mapper.Db.Map props)
    {
        Mapper.Db.Signal[] sources = {source};
        mmon_map_or_mod_db_sigs(_monitor, sources, dest, props, 1);
    }
    public void modifyMap(Mapper.Db.Signal[] sources, Mapper.Db.Signal dest,
                          Mapper.Db.Map props)
    {
        mmon_map_or_mod_db_sigs(_monitor, sources, dest, props, 1);
    }

    public void autosubscribe(int autosubscribeFlags)
    {
        mmon_autosubscribe(_monitor, autosubscribeFlags);
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

    private native long mmon_new(int autosubscribe_flags);
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
    private native void mmon_autosubscribe(long mon, int autosubscribe_flags);
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
