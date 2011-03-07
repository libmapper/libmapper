
package Mapper;

public class Device
{
    public Device(String name, int port) {
        _device = device_new(name, port);
    }

    public void free() {
        if (_device!=0)
            device_free(_device);
        _device = 0;
    }

    public int poll(int timeout) {
        return device_poll(_device, timeout);
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

    private native long device_new(String name, int port);
    private native void device_free(long _d);
    private native int device_poll(long _d, int timeout);

    private long _device;

    static { 
        System.loadLibrary("mapperjni-0");
    } 
}
