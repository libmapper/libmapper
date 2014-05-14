
package Mapper;

import Mapper.NativeLib;
import Mapper.PropertyValue;
import Mapper.TimeTag;
import Mapper.Db.*;

public class Monitor
{
    /*! Bit flags for coordinating monitor metadata subscriptions. */
    public static final int SUB_DEVICE                  = 0x01;
    public static final int SUB_DEVICE_INPUTS           = 0x02;
    public static final int SUB_DEVICE_OUTPUTS          = 0x04;
    public static final int SUB_DEVICE_SIGNALS          = 0x06;
    public static final int SUB_DEVICE_LINKS_IN         = 0x08;
    public static final int SUB_DEVICE_LINKS_OUT        = 0x10;
    public static final int SUB_DEVICE_LINKS            = 0x18;
    public static final int SUB_DEVICE_CONNECTIONS_IN   = 0x20;
    public static final int SUB_DEVICE_CONNECTIONS_OUT  = 0x40;
    public static final int SUB_DEVICE_CONNECTIONS      = 0x60;
    public static final int SUB_DEVICE_ALL              = 0xFF;

    public Monitor(int autosubscribe_flags) {
        _monitor = mapper_monitor_new(autosubscribe_flags);
    }
    public Monitor() {
        _monitor = mapper_monitor_new(0);
    }

    public void free() {
        if (_monitor!=0)
            mapper_monitor_free(_monitor);
        _monitor = 0;
    }

    public int poll(int timeout) {
        return mapper_monitor_poll(_monitor, timeout);
    }

    public class Db {

        private Db(long d, Monitor m) { _db = d; _monitor = m; }

        private void checkMonitor() {
            if (_monitor._monitor == 0)
                throw new NullPointerException(
                    "Db object associated with invalid Monitor");
        }

        public boolean valid() {
            return _monitor._monitor != 0;
        }

        // add callbacks for device, signal, link, connection

        public void addDeviceCallback(Mapper.Db.DeviceListener handler)
        {
            mmon_add_device_callback(_db, handler);
        }
        private native void mmon_add_device_callback(long _d, Mapper.Db.DeviceListener handler);

        public void removeDeviceCallback(Mapper.Db.DeviceListener handler)
        {
            mmon_remove_device_callback(_db, handler);
        }
        private native void mmon_remove_device_callback(long _d, Mapper.Db.DeviceListener handler);

        public void addSignalCallback(Mapper.Db.SignalListener handler)
        {
            mmon_add_signal_callback(_db, handler);
        }
        private native void mmon_add_signal_callback(long _d, Mapper.Db.SignalListener handler);

        public void removeSignalCallback(Mapper.Db.SignalListener handler)
        {
            mmon_remove_signal_callback(_db, handler);
        }
        private native void mmon_remove_signal_callback(long _d, Mapper.Db.SignalListener handler);

        public void addLinkCallback(Mapper.Db.LinkListener handler)
        {
            mmon_add_link_callback(_db, handler);
        }
        private native void mmon_add_link_callback(long _p, Mapper.Db.LinkListener handler);

        public void removeLinkCallback(Mapper.Db.LinkListener handler)
        {
            mmon_remove_link_callback(_db, handler);
        }
        private native void mmon_remove_link_callback(long _d, Mapper.Db.LinkListener handler);

        public void addConnectionCallback(Mapper.Db.ConnectionListener handler)
        {
            mmon_add_connection_callback(_db, handler);
        }
        private native void mmon_add_connection_callback(long _d, Mapper.Db.ConnectionListener handler);

        public void removeConnectionCallback(Mapper.Db.ConnectionListener handler)
        {
            mmon_remove_connection_callback(_db, handler);
        }
        private native void mmon_remove_connection_callback(long _d, Mapper.Db.ConnectionListener handler);

        private long _db;
        private Monitor _monitor;
    };

    public void subscribe(String device_name, int subscribe_flags, int timeout)
    {
        mapper_monitor_subscribe(_monitor, device_name, subscribe_flags, timeout);
    }

    public void unsubscribe(String device_name)
    {
        mapper_monitor_unsubscribe(_monitor, device_name);
    }

    public void link(String source_device, String dest_device,
                     Mapper.Db.Link props)
    {
        // separate props and props flags
        mapper_monitor_link(_monitor, source_device, dest_device, props);
    }

    public void unlink(String source_device, String dest_device)
    {
        mapper_monitor_unlink(_monitor, source_device, dest_device);
    }

    public void connect(String source_signal, String dest_signal,
                        Mapper.Db.Connection props)
    {
        mapper_monitor_connect(_monitor, source_signal, dest_signal, props);
    }

    public void disconnect(String source_signal, String dest_signal)
    {
        mapper_monitor_disconnect(_monitor, source_signal, dest_signal);
    }

    public void connection_modify(String source_signal, String dest_signal,
                                  Mapper.Db.Connection props)
    {
        // separate the props from the flags
        mapper_monitor_connection_modify(_monitor, source_signal,
                                         dest_signal, props);
    }

    public void autosubscribe(int autosubscribe_flags)
    {
        mapper_monitor_autosubscribe(_monitor, autosubscribe_flags);
    }

    public TimeTag now()
    {
        return mapper_monitor_now(_monitor);
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

    private native long mapper_monitor_new(int autosubscribe_flags);
    private native void mapper_monitor_free(long _d);
    private native int mapper_monitor_poll(long _d, int timeout);
    private native void mapper_monitor_subscribe(long _d, String device_name,
                                                 int subscribe_flags, int timeout);
    private native void mapper_monitor_unsubscribe(long _d, String device_name);
    private native void mapper_monitor_link(long _d, String source_device,
                                            String dest_device,
                                            Mapper.Db.Link props);
    private native void mapper_monitor_unlink(long _d, String source_device,
                                              String dest_device);
    private native void mapper_monitor_connect(long _d, String source_signal,
                                               String dest_signal,
                                               Mapper.Db.Connection props);
    private native void mapper_monitor_connection_modify(long _d,
                                                         String source_signal,
                                                         String dest_signal,
                                                         Mapper.Db.Connection props);
    private native void mapper_monitor_disconnect(long _d, String source_signal,
                                                  String dest_signal);
    private native void mapper_monitor_autosubscribe(long _d,
                                                     int autosubscribe_flags);
    private native TimeTag mapper_monitor_now(long _d);

    private long _monitor;
    public boolean valid() {
        return _monitor != 0;
    }

    static { 
        System.loadLibrary(NativeLib.name);
    } 
}
