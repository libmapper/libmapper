
package Mapper;

import Mapper.NativeLib;
import Mapper.PropertyValue;
import Mapper.Db.*;

public class Device
{
    public Device(String name, int port) {
        _device = mdev_new(name, port);
    }

    public void free() {
        if (_device!=0)
            mdev_free(_device);
        _device = 0;
    }

    public int poll(int timeout) {
        return mdev_poll(_device, timeout);
    }

    public class Signal {

        /*! Describes the voice-stealing mode for instances.
         *  Arguments to set_instance_allocation_mode(). */
        public static final int IN_UNDEFINED = 1;
        public static final int IN_STEAL_OLDEST = 2;
        public static final int IN_STEAL_NEWEST = 3;
        public static final int N_MAPPER_INSTANCE_ALLOCATION_TYPES = 4;

        private Signal(long s, Device d) { _signal = s; _device = d; }

        public String name()
        {
            checkDevice();
            return msig_name(_signal);
        }
        public String full_name()
        {
            checkDevice();
            return msig_full_name(_signal);
        }
        public boolean is_output()
        {
            checkDevice();
            return msig_is_output(_signal);
        }
        public void set_minimum(Double minimum) {
            checkDevice();
            msig_set_minimum(_signal, minimum);
        }
        public void set_maximum(Double maximum) {
            checkDevice();
            msig_set_maximum(_signal, maximum);
        }
        public void set_rate(double rate) {
            checkDevice();
            msig_set_rate(_signal, rate);
        }
        public void set_property(String property, PropertyValue p)
        {
            checkDevice();
            msig_set_property(_signal, property, p);
        }
        public void remove_property(String property)
        {
            checkDevice();
            msig_remove_property(_signal, property);
        }
        public int query_remotes(TimeTag tt)
        {
            checkDevice();
            return msig_query_remotes(_signal, tt);
        }
        public int query_remotes()
        {
            checkDevice();
            return msig_query_remotes(_signal, null);
        }

        private native String msig_full_name(long sig);
        private native String msig_name(long sig);
        private native boolean msig_is_output(long sig);
        private native void msig_set_minimum(long sig, Double minimum);
        private native void msig_set_maximum(long sig, Double maximum);
        private native void msig_set_rate(long sig, double rate);
        public native Mapper.Db.Signal properties();
        private native void msig_set_property(long sig, String property,
                                              PropertyValue p);
        private native void msig_remove_property(long sig, String property);
        private native int msig_query_remotes(long sig, TimeTag tt);

        public native void set_instance_event_callback(
            InstanceEventListener handler, int flags);
        public native void set_callback(InputListener handler);

        public native void set_instance_callback(int instance_id,
                                                 InputListener cb);
        public native InputListener get_instance_callback(int instance_id);

        public native void reserve_instances(int num);
        public native void release_instance(int instance_id, TimeTag tt);
        public void release_instance(int instance_id)
            { release_instance(instance_id, null); }

        public native Integer oldest_active_instance();
        public native Integer newest_active_instance();

        public native void match_instances(Signal from, Signal to,
                                           int instance_id);
        public native int num_active_instances();
        public native int num_reserved_instances();
        public native int active_instance_id(int index);
        public native void set_instance_allocation_mode(int mode);
        public native int instance_allocation_mode();

        public native int num_connections();

        public native void update(int value);
        public native void update(float value);
        public native void update(double value);
        public native void update(int[] value);
        public native void update(float[] value);
        public native void update(double[] value);

        public native void update(int value, TimeTag tt);
        public native void update(float value, TimeTag tt);
        public native void update(double value, TimeTag tt);
        public native void update(int[] value, TimeTag tt);
        public native void update(float[] value, TimeTag tt);
        public native void update(double[] value, TimeTag tt);

        public void update_instance(int instance_id, int value)
            { update_instance(instance_id, value, null); }
        public void update_instance(int instance_id, float value)
            { update_instance(instance_id, value, null); }
        public void update_instance(int instance_id, double value)
            { update_instance(instance_id, value, null); }
        public void update_instance(int instance_id, int[] value)
            { update_instance(instance_id, value, null); }
        public void update_instance(int instance_id, float[] value)
            { update_instance(instance_id, value, null); }
        public void update_instance(int instance_id, double[] value)
            { update_instance(instance_id, value, null); }

        public native void update_instance(int instance_id,
                                           int value, TimeTag tt);
        public native void update_instance(int instance_id,
                                           float value, TimeTag tt);
        public native void update_instance(int instance_id,
                                           double value, TimeTag tt);
        public native void update_instance(int instance_id,
                                           int[] value, TimeTag tt);
        public native void update_instance(int instance_id,
                                           float[] value, TimeTag tt);
        public native void update_instance(int instance_id,
                                           double[] value, TimeTag tt);

        public native boolean value(int[] value, TimeTag tt);
        public native boolean value(float[] value, TimeTag tt);
        public native boolean value(double[] value, TimeTag tt);

        public boolean value(int[] value)
            { return value(value, null); }
        public boolean value(float[] value)
            { return value(value, null); }
        public boolean value(double[] value)
            { return value(value, null); }

        public native boolean instance_value(int instance_id,
                                             int[] value, TimeTag tt);
        public native boolean instance_value(int instance_id,
                                             float[] value, TimeTag tt);
        public native boolean instance_value(int instance_id,
                                             double[] value, TimeTag tt);

        public boolean instance_value(int instance_id, int[] value)
            { return instance_value(instance_id, value, null); }
        public boolean instance_value(int instance_id, float[] value)
            { return instance_value(instance_id, value, null); }
        public boolean instance_value(int instance_id, double[] value)
            { return instance_value(instance_id, value, null); }

        public int index()
        {
            checkDevice();
            if (_index==null) {
                _index = new Integer(-1);
                long msig = 0;
                if (is_output())
                    msig = mdev_get_output_by_name(_device._device,
                                                   msig_name(_signal),
                                                   _index);
                else
                    msig = mdev_get_input_by_name(_device._device,
                                                  msig_name(_signal),
                                                  _index);
                if (msig==0)
                    _index = null;
            }
            return _index;
        }

        private void checkDevice() {
            if (_device._device == 0)
                throw new NullPointerException(
                    "Signal object associated with invalid Device");
        }

        public boolean valid() {
            return _device._device != 0;
        }

        private long _signal;
        private Device _device;
        private Integer _index;
    };

    public void remove_input(Signal sig)
    {
        mdev_remove_input(_device, sig._signal);
    }

    public void remove_output(Signal sig)
    {
        mdev_remove_output(_device, sig._signal);
    }

    public int num_inputs()
    {
        return mdev_num_inputs(_device);
    }

    public int num_outputs()
    {
        return mdev_num_outputs(_device);
    }

    public int num_links_in()
    {
        return mdev_num_links_in(_device);
    }

    public int num_links_out()
    {
        return mdev_num_links_out(_device);
    }

    public int num_connections_in()
    {
        return mdev_num_connections_in(_device);
    }

    public int num_connections_out()
    {
        return mdev_num_connections_out(_device);
    }

    public Signal get_input_by_name(String name)
    {
        long msig = mdev_get_input_by_name(_device, name, null);
        return msig==0 ? null : new Signal(msig, this);
    }

    public Signal get_output_by_name(String name)
    {
        long msig = mdev_get_output_by_name(_device, name, null);
        return msig==0 ? null : new Signal(msig, this);
    }

    public Signal get_input_by_index(int index)
    {
        long msig = mdev_get_input_by_index(_device, index);
        return msig==0 ? null : new Signal(msig, this);
    }

    public Signal get_output_by_index(int index)
    {
        long msig = mdev_get_output_by_index(_device, index);
        return msig==0 ? null : new Signal(msig, this);
    }

    public void set_property(String property, PropertyValue p)
    {
        mdev_set_property(_device, property, p);
    }

    public void remove_property(String property)
    {
        mdev_remove_property(_device, property);
    }

    public boolean ready()
    {
        return mdev_ready(_device);
    }

    public String name()
    {
        return mdev_name(_device);
    }

    public int port()
    {
        return mdev_port(_device);
    }

    public String ip4()
    {
        return mdev_ip4(_device);
    }

    public String iface()
    {
        return mdev_interface(_device);
    }

    public int ordinal()
    {
        return mdev_ordinal(_device);
    }

    public int id()
    {
        return mdev_id(_device);
    }

    public void start_queue(TimeTag tt)
    {
        mdev_start_queue(_device, tt);
    }

    public void send_queue(TimeTag tt)
    {
        mdev_send_queue(_device, tt);
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

    private native long mdev_new(String name, int port);
    private native void mdev_free(long _d);
    private native int mdev_poll(long _d, int timeout);
    private native void mdev_remove_input(long _d, long _sig);
    private native void mdev_remove_output(long _d, long _sig);
    private native int mdev_num_inputs(long _d);
    private native int mdev_num_outputs(long _d);
    private native int mdev_num_links_in(long _d);
    private native int mdev_num_links_out(long _d);
    private native int mdev_num_connections_in(long _d);
    private native int mdev_num_connections_out(long _d);
    private native long mdev_get_input_by_name(long _d, String name,
                                               Integer index);
    private native long mdev_get_output_by_name(long _d, String name,
                                                Integer index);
    private native long mdev_get_input_by_index(long _d, int index);
    private native long mdev_get_output_by_index(long _d, int index);
    private native void mdev_set_property(long _d, String property,
                                          PropertyValue p);
    private native void mdev_remove_property(long _d, String property);
    private native boolean mdev_ready(long _d);
    private native String mdev_name(long _d);
    private native int mdev_port(long _d);
    private native String mdev_ip4(long _d);
    private native String mdev_interface(long _d);
    private native int mdev_ordinal(long _d);
    private native int mdev_id(long _d);
    private native void mdev_start_queue(long _d, TimeTag tt);
    private native void mdev_send_queue(long _d, TimeTag tt);

    public native Signal add_input(String name, int length,
                                   char type, String unit,
                                   Double minimum, Double maximum,
                                   InputListener handler);

    public native Signal add_output(String name, int length,
                                    char type, String unit,
                                    Double minimum, Double maximum);

    private long _device;
    public boolean valid() {
        return _device != 0;
    }

    static { 
        System.loadLibrary(NativeLib.name);
    } 
}
