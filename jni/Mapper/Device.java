
package Mapper;

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

    private class Signal {
        public Signal(long s) { _signal = s; }
        private long _signal;
    };

    public Signal add_input(String name, int length, char type,
                            String unit, Double minimum,
                            Double maximum, InputListener handler)
    {
        long msig = mdev_add_input(_device, name, length, type, unit,
                                   minimum, maximum, handler);
        return msig==0 ? null : new Signal(msig);
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
    private native long mdev_add_input(long _d, String name, int length,
                                       char type, String unit,
                                       Double minimum, Double maximum,
                                       InputListener handler);

    private long _device;

    static { 
        System.loadLibrary("mapperjni-0");
    } 
}
