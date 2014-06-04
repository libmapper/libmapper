
package Mapper;

import Mapper.NativeLib;
import Mapper.PropertyValue;
import Mapper.Db.*;

public class Device
{
    /*! The set of possible actions on a local device link or
     *  connection. */
    public static final int MDEV_LOCAL_ESTABLISHED  = 0;
    public static final int MDEV_LOCAL_MODIFIED     = 1;
    public static final int MDEV_LOCAL_DESTROYED    = 2;

    public Device(String name) {
        _device = mdev_new(name, 0);
    }
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

    public int poll() {
        return mdev_poll(_device, 0);
    }

    public class Signal {

        /*! Describes the voice-stealing mode for instances.
         *  Arguments to set_instance_allocation_mode(). */
        public static final int IN_UNDEFINED                        = 0;
        public static final int IN_STEAL_OLDEST                     = 1;
        public static final int IN_STEAL_NEWEST                     = 2;

        private Signal(long s, Device d) { _signal = s; _device = d; }

        public String name()
        {
            checkDevice();
            return msig_name(_signal);
        }
        public String fullName()
        {
            checkDevice();
            return msig_full_name(_signal);
        }
        public boolean isOutput()
        {
            checkDevice();
            return msig_is_output(_signal);
        }
        public void setMinimum(PropertyValue p) {
            checkDevice();
            msig_set_property(_signal, new String("min"), p);
        }
        public void setMaximum(PropertyValue p) {
            checkDevice();
            msig_set_property(_signal, new String("max"), p);
        }
        public void setRate(double rate) {
            checkDevice();
            msig_set_rate(_signal, rate);
        }
        public void setProperty(String property, PropertyValue p)
        {
            checkDevice();
            msig_set_property(_signal, property, p);
        }
        public void removeProperty(String property)
        {
            checkDevice();
            msig_remove_property(_signal, property);
        }

        private native String msig_full_name(long sig);
        private native String msig_name(long sig);
        private native boolean msig_is_output(long sig);
        private native void msig_set_rate(long sig, double rate);
        public native Mapper.Db.Signal properties();
        private native void msig_set_property(long sig, String property,
                                              PropertyValue p);
        private native void msig_remove_property(long sig, String property);

        public native void setInstanceEventCallback(
            InstanceEventListener handler, int flags);
        public native void setCallback(InputListener handler);

        public native void setInstanceCallback(int instanceId,
                                               InputListener cb);
        public native InputListener getInstanceCallback(int instanceId);

        public native int reserveInstances(int[] ids, int num, InputListener cb);
        public int reserveInstances(int num)
            { return reserveInstances(null, num, null); }
        public int reserveInstances(int[] ids)
            { return reserveInstances(ids, 0, null); }
        public int reserveInstances(int num, InputListener cb)
            { return reserveInstances(null, num, cb); }
        public int reserveInstances(int[] ids, InputListener cb)
            { return reserveInstances(ids, 0, cb); }


        public native void releaseInstance(int instanceId, TimeTag tt);
        public void releaseInstance(int instanceId)
            { releaseInstance(instanceId, null); }
        public native void removeInstance(int num);

        public native Integer oldestActiveInstance();
        public native Integer newestActiveInstance();

        public native void matchInstances(Signal from, Signal to,
                                          int instanceId);
        public native int numActiveInstances();
        public native int numReservedInstances();
        public native int activeInstanceId(int index);
        public native void setInstanceAllocationMode(int mode);
        public native int instanceAllocationMode();

        public native int numConnections();

        public native void updateInstance(int instanceId,
                                          int value, TimeTag tt);
        public native void updateInstance(int instanceId,
                                          float value, TimeTag tt);
        public native void updateInstance(int instanceId,
                                          double value, TimeTag tt);
        public native void updateInstance(int instanceId,
                                          int[] value, TimeTag tt);
        public native void updateInstance(int instanceId,
                                          float[] value, TimeTag tt);
        public native void updateInstance(int instanceId,
                                          double[] value, TimeTag tt);

        public void updateInstance(int instanceId, int value)
            { updateInstance(instanceId, value, null); }
        public void updateInstance(int instanceId, float value)
            { updateInstance(instanceId, value, null); }
        public void updateInstance(int instanceId, double value)
            { updateInstance(instanceId, value, null); }
        public void updateInstance(int instanceId, int[] value)
            { updateInstance(instanceId, value, null); }
        public void updateInstance(int instanceId, float[] value)
            { updateInstance(instanceId, value, null); }
        public void updateInstance(int instanceId, double[] value)
            { updateInstance(instanceId, value, null); }

        public void update(int value)
            { updateInstance(0, value, null); }
        public void update(float value)
            { updateInstance(0, value, null); }
        public void update(double value)
            { updateInstance(0, value, null); }
        public void update(int[] value)
            { updateInstance(0, value, null); }
        public void update(float[] value)
            { updateInstance(0, value, null); }
        public void update(double[] value)
            { updateInstance(0, value, null); }

        public void update(int value, TimeTag tt)
            { updateInstance(0, value, tt); }
        public void update(float value, TimeTag tt)
            { updateInstance(0, value, tt); }
        public void update(double value, TimeTag tt)
            { updateInstance(0, value, tt); }
        public void update(int[] value, TimeTag tt)
            { updateInstance(0, value, tt); }
        public void update(float[] value, TimeTag tt)
            { updateInstance(0, value, tt); }
        public void update(double[] value, TimeTag tt)
            { updateInstance(0, value, tt); }

        public native boolean instanceValue(int instanceId,
                                            int[] value, TimeTag tt);
        public native boolean instanceValue(int instanceId,
                                            float[] value, TimeTag tt);
        public native boolean instanceValue(int instanceId,
                                            double[] value, TimeTag tt);

        public boolean instanceValue(int instanceId, int[] value)
            { return instanceValue(instanceId, value, null); }
        public boolean instanceValue(int instanceId, float[] value)
            { return instanceValue(instanceId, value, null); }
        public boolean instanceValue(int instanceId, double[] value)
            { return instanceValue(instanceId, value, null); }

        public boolean value(int[] value, TimeTag tt)
            { return instanceValue(0, value, tt); }
        public boolean value(float[] value, TimeTag tt)
            { return instanceValue(0, value, tt); }
        public boolean value(double[] value, TimeTag tt)
            { return instanceValue(0, value, tt); }

        public boolean value(int[] value)
            { return instanceValue(0, value, null); }
        public boolean value(float[] value)
            { return instanceValue(0, value, null); }
        public boolean value(double[] value)
            { return instanceValue(0, value, null); }

        public native int queryRemotes(TimeTag tt);
        public int queryRemotes()
            { return queryRemotes(null); };

        public int index()
        {
            checkDevice();
            if (_index==null) {
                _index = new Integer(-1);
                long msig = 0;
                if (isOutput())
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

    public void removeInput(Signal sig)
    {
        mdev_remove_input(_device, sig._signal);
    }

    public void removeOutput(Signal sig)
    {
        mdev_remove_output(_device, sig._signal);
    }

    public int numInputs()
    {
        return mdev_num_inputs(_device);
    }

    public int numOutputs()
    {
        return mdev_num_outputs(_device);
    }

    public int numLinksIn()
    {
        return mdev_num_links_in(_device);
    }

    public int numLinksOut()
    {
        return mdev_num_links_out(_device);
    }

    public int numConnectionsIn()
    {
        return mdev_num_connections_in(_device);
    }

    public int numConnectionsOut()
    {
        return mdev_num_connections_out(_device);
    }

    public Signal getInput(String name)
    {
        long msig = mdev_get_input_by_name(_device, name, null);
        return msig==0 ? null : new Signal(msig, this);
    }

    public Signal getOutput(String name)
    {
        long msig = mdev_get_output_by_name(_device, name, null);
        return msig==0 ? null : new Signal(msig, this);
    }

    public Signal getInput(int index)
    {
        long msig = mdev_get_input_by_index(_device, index);
        return msig==0 ? null : new Signal(msig, this);
    }

    public Signal getOutput(int index)
    {
        long msig = mdev_get_output_by_index(_device, index);
        return msig==0 ? null : new Signal(msig, this);
    }

    public void setProperty(String property, PropertyValue p)
    {
        mdev_set_property(_device, property, p);
    }

    public void removeProperty(String property)
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

    public void startQueue(TimeTag tt)
    {
        mdev_start_queue(_device, tt);
    }

    public void sendQueue(TimeTag tt)
    {
        mdev_send_queue(_device, tt);
    }

    public TimeTag now()
    {
        return mdev_now(_device);
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
    public native Mapper.Db.Device properties();
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
    private native TimeTag mdev_now(long _d);

    public native Signal addInput(String name, int length, char type, String unit,
                                  PropertyValue minimum, PropertyValue maximum,
                                  InputListener handler);

    public native Signal addOutput(String name, int length, char type, String unit,
                                   PropertyValue minimum, PropertyValue maximum);

    private long _device;
    public boolean valid() {
        return _device != 0;
    }

    static {
        System.loadLibrary(NativeLib.name);
    }
}
