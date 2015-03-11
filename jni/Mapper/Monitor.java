
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
    public static final int SUBSCRIBE_CONNECTIONS_IN    = 0x10;
    public static final int SUBSCRIBE_CONNECTIONS_OUT   = 0x20;
    public static final int SUBSCRIBE_CONNECTIONS       = 0x30;
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
            _connection_cb = null;
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

        public void addConnectionCallback(Mapper.Db.ConnectionListener handler)
        {
            if (handler != _connection_cb)
                mdb_remove_connection_callback(_db, _connection_cb);
            mdb_add_connection_callback(_db, handler);
            _connection_cb = handler;
        }
        private native void mdb_add_connection_callback(long db, Mapper.Db.ConnectionListener handler);

        public void removeConnectionCallback(Mapper.Db.ConnectionListener handler)
        {
            mdb_remove_connection_callback(_db, handler);
        }
        private native void mdb_remove_connection_callback(long db, Mapper.Db.ConnectionListener handler);

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

        // Db.Connection
        private native long mdb_connections(long db, String deviceName);
        public Mapper.Db.ConnectionCollection connections()
        {
            long _c = mdb_connections(_db, null);
            return new Mapper.Db.ConnectionCollection(_c);
        }
        public Mapper.Db.ConnectionCollection connections(String deviceName)
        {
            long _c = mdb_connections(_db, deviceName);
            return new Mapper.Db.ConnectionCollection(_c);
        }
        public Mapper.Db.ConnectionCollection connections(Mapper.Device device)
        {
            long _c = mdb_connections(_db, device.name());
            return new Mapper.Db.ConnectionCollection(_c);
        }
        public Mapper.Db.ConnectionCollection connections(Mapper.Db.Device device)
        {
            long _c = mdb_connections(_db, device.name());
            return new Mapper.Db.ConnectionCollection(_c);
        }

        public native Mapper.Db.ConnectionCollection connections(Mapper.Db.SignalCollection src,
                                                                 Mapper.Db.SignalCollection dest);
        public native Mapper.Db.ConnectionCollection connections(String[] srcDevice,
                                                                 String destDevice);
        public Mapper.Db.ConnectionCollection connections(String source, String dest)
        {
            String[] sources = {source};
            return this.connections(sources, dest);
        }
        public Mapper.Db.ConnectionCollection connections(Mapper.Device[] sources,
                                                          Mapper.Device dest)
        {
            int length = sources.length;
            String[] srcNames = new String[length];
            for (int i = 0; i < length; i++)
                srcNames[i] = sources[i].name();
            return this.connections(srcNames, dest.name());
        }
        public Mapper.Db.ConnectionCollection connections(Mapper.Device source,
                                                          Mapper.Device dest)
        {
            Mapper.Device[] sources = {source};
            return this.connections(sources, dest);
        }
        public Mapper.Db.ConnectionCollection connections(Mapper.Db.Device[] sources,
                                                          Mapper.Db.Device dest)
        {
            int length = sources.length;
            String[] srcNames = new String[length];
            for (int i = 0; i < length; i++)
                srcNames[i] = sources[i].name();
            return this.connections(srcNames, dest.name());
        }
        public Mapper.Db.ConnectionCollection connections(Mapper.Db.Device source,
                                                          Mapper.Db.Device dest)
        {
            Mapper.Db.Device[] sources = {source};
            return this.connections(sources, dest);
        }

        private native Mapper.Db.ConnectionCollection mdb_connections_by_src(
            long db, String deviceName, String signalName);
        public Mapper.Db.ConnectionCollection connectionsBySrc(String signalName)
            { return mdb_connections_by_src(_db, null, signalName); }
        public Mapper.Db.ConnectionCollection connectionsBySrc(String deviceName,
                                                               String signalName)
            { return mdb_connections_by_src(_db, deviceName, signalName); }
        public Mapper.Db.ConnectionCollection connectionsBySrc(Mapper.Device.Signal signal)
            { return mdb_connections_by_src(_db, null, signal.fullName()); }
        public Mapper.Db.ConnectionCollection connectionsBySrc(Mapper.Db.Signal signal)
            { return mdb_connections_by_src(_db, null, signal.fullName()); }

        private native Mapper.Db.ConnectionCollection mdb_connections_by_dest(
            long db, String deviceName, String signalName);
        public Mapper.Db.ConnectionCollection connectionsByDest(String signalName)
            { return mdb_connections_by_dest(_db, null, signalName); }
        public Mapper.Db.ConnectionCollection connectionsByDest(String deviceName,
                                                                String signalName)
            { return mdb_connections_by_dest(_db, deviceName, signalName); }
        public Mapper.Db.ConnectionCollection connectionsByDest(Mapper.Device.Signal signal)
            { return mdb_connections_by_dest(_db, null, signal.fullName()); }
        public Mapper.Db.ConnectionCollection connectionsByDest(Mapper.Db.Signal signal)
            { return mdb_connections_by_dest(_db, null, signal.fullName()); }

        public native Mapper.Db.Connection connection(String[] srcNames,
                                                      String destName);
        public Mapper.Db.Connection connection(Mapper.Device.Signal[] sources,
                                               Mapper.Device.Signal dest)
        {
            int length = sources.length;
            String[] srcNames = new String[length];
            for (int i = 0; i < length; i++)
                srcNames[i] = sources[i].fullName();
            return this.connection(srcNames, dest.fullName());
        }
        public Mapper.Db.Connection connection(Mapper.Db.Signal[] sources,
                                               Mapper.Db.Signal dest)
        {
            int length = sources.length;
            String[] srcNames = new String[length];
            for (int i = 0; i < length; i++)
                srcNames[i] = sources[i].fullName();
            return this.connection(srcNames, dest.fullName());
        }

        private long _db;
        private Monitor _monitor;

        // TODO: enable multiple listeners
        private Mapper.Db.DeviceListener _device_cb;
        private Mapper.Db.SignalListener _signal_cb;
        private Mapper.Db.ConnectionListener _connection_cb;
    };

    public void subscribe(String deviceName, int subscribeFlags, int timeout)
    {
        mmon_subscribe(_monitor, deviceName, subscribeFlags, timeout);
    }

    public void unsubscribe(String deviceName)
    {
        mmon_unsubscribe(_monitor, deviceName);
    }

    public void connect(Mapper.Device.Signal source, Mapper.Device.Signal dest)
    {
        Mapper.Device.Signal[] sources = {source};
        mmon_connect_or_mod_sigs(_monitor, sources, dest, null, 0);
    }
    public void connect(Mapper.Device.Signal[] sources, Mapper.Device.Signal dest)
    {
        mmon_connect_or_mod_sigs(_monitor, sources, dest, null, 0);
    }
    public void connect(Mapper.Device.Signal source, Mapper.Device.Signal dest,
                        Mapper.Db.Connection props)
    {
        Mapper.Device.Signal[] sources = {source};
        mmon_connect_or_mod_sigs(_monitor, sources, dest, props, 0);
    }
    public void connect(Mapper.Device.Signal[] sources, Mapper.Device.Signal dest,
                        Mapper.Db.Connection props)
    {
        mmon_connect_or_mod_sigs(_monitor, sources, dest, props, 0);
    }
    public void connect(Mapper.Db.Signal source, Mapper.Db.Signal dest)
    {
        Mapper.Db.Signal[] sources = {source};
        mmon_connect_or_mod_db_sigs(_monitor, sources, dest, null, 0);
    }
    public void connect(Mapper.Db.Signal[] sources, Mapper.Db.Signal dest)
    {
        mmon_connect_or_mod_db_sigs(_monitor, sources, dest, null, 0);
    }
    public void connect(Mapper.Db.Signal source, Mapper.Db.Signal dest,
                        Mapper.Db.Connection props)
    {
        Mapper.Db.Signal[] sources = {source};
        mmon_connect_or_mod_db_sigs(_monitor, sources, dest, props, 0);
    }
    public void connect(Mapper.Db.Signal[] sources, Mapper.Db.Signal dest,
                        Mapper.Db.Connection props)
    {
        mmon_connect_or_mod_db_sigs(_monitor, sources, dest, props, 0);
    }

    public void disconnect(Mapper.Db.Connection connection)
    {
        mmon_disconnect(_monitor, connection);
    }
    public void disconnect(Mapper.Device.Signal source,
                           Mapper.Device.Signal dest)
    {
        Mapper.Device.Signal[] sources = {source};
        mmon_disconnect_sigs(_monitor, sources, dest);
    }
    public void disconnect(Mapper.Device.Signal[] sources,
                           Mapper.Device.Signal dest)
    {
        mmon_disconnect_sigs(_monitor, sources, dest);
    }
    public void disconnect(Mapper.Db.Signal source,
                           Mapper.Db.Signal dest)
    {
        Mapper.Db.Signal[] sources = {source};
        mmon_disconnect_db_sigs(_monitor, sources, dest);
    }
    public void disconnect(Mapper.Db.Signal[] sources,
                           Mapper.Db.Signal dest)
    {
        mmon_disconnect_db_sigs(_monitor, sources, dest);
    }

    public void modifyConnection(Mapper.Device.Signal source,
                                 Mapper.Device.Signal dest,
                                 Mapper.Db.Connection props)
    {
        Mapper.Device.Signal[] sources = {source};
        mmon_connect_or_mod_sigs(_monitor, sources, dest, props, 1);
    }
    public void modifyConnection(Mapper.Device.Signal[] sources,
                                 Mapper.Device.Signal dest,
                                 Mapper.Db.Connection props)
    {
        mmon_connect_or_mod_sigs(_monitor, sources, dest, props, 1);
    }
    public void modifyConnection(Mapper.Db.Signal source,
                                 Mapper.Db.Signal dest,
                                 Mapper.Db.Connection props)
    {
        Mapper.Db.Signal[] sources = {source};
        mmon_connect_or_mod_db_sigs(_monitor, sources, dest, props, 1);
    }
    public void modifyConnection(Mapper.Db.Signal[] sources,
                                 Mapper.Db.Signal dest,
                                 Mapper.Db.Connection props)
    {
        mmon_connect_or_mod_db_sigs(_monitor, sources, dest, props, 1);
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
    private native void mmon_connect_or_mod_sigs(long mon,
                                                 Mapper.Device.Signal[] sources,
                                                 Mapper.Device.Signal dest,
                                                 Mapper.Db.Connection props,
                                                 int modify);
    private native void mmon_connect_or_mod_db_sigs(long mon,
                                                    Mapper.Db.Signal[] sources,
                                                    Mapper.Db.Signal dest,
                                                    Mapper.Db.Connection props,
                                                    int modify);
//    private native void mmon_modify_connection(long mon,
//                                               Mapper.Db.Connection connection);
    private native void mmon_disconnect(long mon,
                                        Mapper.Db.Connection connection);
    private native void mmon_disconnect_sigs(long mon,
                                             Mapper.Device.Signal[] sources,
                                             Mapper.Device.Signal dest);
    private native void mmon_disconnect_db_sigs(long mon,
                                                Mapper.Db.Signal[] sources,
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
