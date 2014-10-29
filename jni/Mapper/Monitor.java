
package Mapper;

import Mapper.NativeLib;
import Mapper.PropertyValue;
import Mapper.TimeTag;
import Mapper.Db.*;

public class Monitor
{
    /*! Bit flags for coordinating monitor metadata subscriptions. */
    public static final int SUB_NONE                    = 0x00;
    public static final int SUB_DEVICE                  = 0x01;
    public static final int SUB_DEVICE_INPUTS           = 0x03;
    public static final int SUB_DEVICE_OUTPUTS          = 0x05;
    public static final int SUB_DEVICE_SIGNALS          = 0x07;
    public static final int SUB_DEVICE_LINKS            = 0x09;
    public static final int SUB_DEVICE_CONNECTIONS_IN   = 0x21;
    public static final int SUB_DEVICE_CONNECTIONS_OUT  = 0x41;
    public static final int SUB_DEVICE_CONNECTIONS      = 0x61;
    public static final int SUB_DEVICE_ALL              = 0xFF;

    public Monitor(int autosubscribeFlags) {
        _monitor = mmon_new(autosubscribeFlags);
        Db = this.new Db(mmon_get_db(_monitor));
    }
    public Monitor() {
        _monitor = mmon_new(0);
        Db = this.new Db(mmon_get_db(_monitor));
    }

    public void free() {
        if (_monitor!=0)
            mmon_free(_monitor);
        _monitor = 0;
    }

    public int poll(int timeout) {
        return mmon_poll(_monitor, timeout);
    }

    public class Db {

        /*! The set of possible statuses for a db entity. */
        public static final int MODIFIED     = 0;
        public static final int NEW          = 1;
        public static final int REMOVED      = 2;
        public static final int UNRESPONSIVE = 3;

        private Db(long d) {
            _db = d;
            _device_cb = null;
            _signal_cb = null;
            _link_cb = null;
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

        public void addLinkCallback(Mapper.Db.LinkListener handler)
        {
            if (handler != _link_cb)
                mdb_remove_link_callback(_db, _link_cb);
            mdb_add_link_callback(_db, handler);
            _link_cb = handler;
        }
        private native void mdb_add_link_callback(long _p, Mapper.Db.LinkListener handler);

        public void removeLinkCallback(Mapper.Db.LinkListener handler)
        {
            mdb_remove_link_callback(_db, handler);
        }
        private native void mdb_remove_link_callback(long db, Mapper.Db.LinkListener handler);

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
        public Mapper.Db.SignalCollection inputs() {
            long _s = mdb_inputs(_db, null);
            return (_s == 0) ? null : new Mapper.Db.SignalCollection(_s);
        }
        public Mapper.Db.SignalCollection inputs(String deviceName) {
            long _s = mdb_inputs(_db, deviceName);
            return (_s == 0) ? null : new Mapper.Db.SignalCollection(_s);
        }
        public native Mapper.Db.SignalCollection matchInputs(String deviceName,
                                                             String signalPattern);

        // Db.Output
        public native Mapper.Db.Signal getOutput(String deviceName,
                                                 String signalName);
        private native long mdb_outputs(long db, String deviceName);
        public Mapper.Db.SignalCollection outputs() {
            long _s = mdb_outputs(_db, null);
            return (_s == 0) ? null : new Mapper.Db.SignalCollection(_s);
        }
        public Mapper.Db.SignalCollection outputs(String deviceName) {
            long _s = mdb_outputs(_db, deviceName);
            return (_s == 0) ? null : new Mapper.Db.SignalCollection(_s);
        }
        public native Mapper.Db.SignalCollection matchOutputs(String deviceName,
                                                              String signalPattern);

        // Db.Link
        public native Mapper.Db.Link getLink(String srcName, String destName);
        private native Mapper.Db.LinkCollection mdb_links(long db, String deviceName);
        public Mapper.Db.LinkCollection links(String deviceName)
            { return mdb_links(_db, deviceName); }
        public Mapper.Db.LinkCollection links()
            { return mdb_links(_db, null); }
        public native Mapper.Db.LinkCollection links(Mapper.Db.DeviceCollection src,
                                                     Mapper.Db.DeviceCollection dest);
        public native Mapper.Db.LinkCollection linksBySrc(String deviceName);
        public native Mapper.Db.LinkCollection linksByDest(String deviceName);

        // Db.Connection
        private native Mapper.Db.ConnectionCollection mdb_connections(
            long db, String deviceName);
        public Mapper.Db.ConnectionCollection connections(String deviceName)
            { return mdb_connections(_db, deviceName); }
        public Mapper.Db.ConnectionCollection connections()
            { return mdb_connections(_db, null); }
        public native Mapper.Db.ConnectionCollection connections(
            Mapper.Db.SignalCollection src, Mapper.Db.SignalCollection dest);
        private native Mapper.Db.ConnectionCollection mdb_connections_by_src(
            long db, String deviceName, String signalName);
        public Mapper.Db.ConnectionCollection connectionsBySrc(String signalName)
            { return mdb_connections_by_src(_db, null, signalName); }
        public Mapper.Db.ConnectionCollection connectionsBySrc(String deviceName,
                                                               String signalName)
            { return mdb_connections_by_src(_db, deviceName, signalName); }
        private native Mapper.Db.ConnectionCollection mdb_connections_by_dest(
            long db, String deviceName, String signalName);
        public Mapper.Db.ConnectionCollection connectionsByDest(String signalName)
            { return mdb_connections_by_dest(_db, null, signalName); }
        public Mapper.Db.ConnectionCollection connectionsByDest(String deviceName,
                                                                String signalName)
            { return mdb_connections_by_dest(_db, deviceName, signalName); }
        public native Mapper.Db.Connection connectionBySignals(String srcName,
                                                               String destName);
        public native Mapper.Db.ConnectionCollection connectionsByDevices(
            String srcDevice, String destDevice);

        private long _db;
        private Monitor _monitor;

        // TODO: enable multiple listeners
        private Mapper.Db.DeviceListener _device_cb;
        private Mapper.Db.SignalListener _signal_cb;
        private Mapper.Db.LinkListener _link_cb;
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

    public void link(String sourceDevice, String destDevice,
                     Mapper.Db.Link props)
    {
        mmon_link(_monitor, sourceDevice, destDevice, props);
    }

    public void unlink(String sourceDevice, String destDevice)
    {
        mmon_unlink(_monitor, sourceDevice, destDevice);
    }

    public void connect(String sourceSignal, String destSignal,
                        Mapper.Db.Connection props)
    {
        mmon_connect_or_mod(_monitor, sourceSignal, destSignal, props, 0);
    }

    public void disconnect(String sourceSignal, String destSignal)
    {
        mmon_disconnect(_monitor, sourceSignal, destSignal);
    }

    public void modifyConnection(String sourceSignal, String destSignal,
                                 Mapper.Db.Connection props)
    {
        mmon_connect_or_mod(_monitor, sourceSignal, destSignal, props, 1);
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
    private native long mmon_get_db(long db);
    private native void mmon_free(long db);
    private native int mmon_poll(long db, int timeout);
    private native void mmon_subscribe(long db, String device_name,
                                       int subscribe_flags, int timeout);
    private native void mmon_unsubscribe(long db, String device_name);
    private native void mmon_link(long db, String source_device,
                                  String dest_device, Mapper.Db.Link props);
    private native void mmon_unlink(long db, String source_device,
                                    String dest_device);
    private native void mmon_connect_or_mod(long db, String source_signal,
                                            String dest_signal,
                                            Mapper.Db.Connection props,
                                            int modify);
    private native void mmon_disconnect(long db, String source_signal,
                                        String dest_signal);
    private native void mmon_autosubscribe(long db, int autosubscribe_flags);
    private native TimeTag mmon_now(long db);

    private long _monitor;
    public Mapper.Monitor.Db Db;
    public boolean valid() {
        return _monitor != 0;
    }

    static {
        System.loadLibrary(NativeLib.name);
    }
}
