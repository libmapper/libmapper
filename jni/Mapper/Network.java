
package mapper;

import mapper.NativeLib;
import mapper.TimeTag;

public class Network
{
    /* constructor */
    private native long mapperNetworkNew(String iface, String group, int port);
    public Network(String iface, String group, int port) {
        _net = mapperNetworkNew(iface, group, port); }
    public Network(String iface) { _net = mapperNetworkNew(iface, null, 0); }
    public Network(long net) { _net = net; }

    /* free */
    private native void mapperNetworkFree(long _n);
    public void free() {
        if (_net != 0)
            mapperNetworkFree(_net);
        _net = 0;
    }

    /* get current time */
    public native TimeTag now();

    public native String iface();

    /* retrieve associated Database */
    private native long mapperNetworkDatabase(long _n);
    public mapper.Database database() {
        return new mapper.Database(mapperNetworkDatabase(_net));
    }

    /* send an arbitrary message */
    // TODO
    // public native Network sendMessage(String path, Object... args);

    // Note: this is _not_ guaranteed to run, the user should still
    // call free() explicitly when the network is no longer needed.
    protected void finalize() throws Throwable {
        try {
            free();
        } finally {
            super.finalize();
        }
    }

    private long _net;
    public boolean valid() {
        return _net != 0;
    }

    static {
        System.loadLibrary(NativeLib.name);
    }
}
