
package Mapper.Db;

import Mapper.PropertyValue;
import Mapper.TimeTag;

public class Device
{
    public Device(long devprops) {
        _devprops = devprops;

        _name = mdb_device_get_name(_devprops);
        _ordinal = mdb_device_get_ordinal(_devprops);
        _host = mdb_device_get_host(_devprops);
        _port = mdb_device_get_port(_devprops);

        _n_inputs = mdb_device_get_num_inputs(_devprops);
        _n_outputs = mdb_device_get_num_outputs(_devprops);
        _n_links_in = mdb_device_get_num_links_in(_devprops);
        _n_links_out = mdb_device_get_num_links_out(_devprops);
        _n_connections_in = mdb_device_get_num_connections_in(_devprops);
        _n_connections_out = mdb_device_get_num_connections_out(_devprops);
        _version = mdb_device_get_version(_devprops);
    }

    private String _name;
    public String name() { return _name; }
    private native String mdb_device_get_name(long p);

    int _ordinal;
    public int ordinal() { return _ordinal; }
    private native int mdb_device_get_ordinal(long p);

    private String _host;
    public String host() { return _host; }
    private native String mdb_device_get_host(long p);

    int _port;
    public int port() { return _port; }
    private native int mdb_device_get_port(long p);

    int _n_inputs;
    public int numInputs() { return _n_inputs; }
    private native int mdb_device_get_num_inputs(long p);

    int _n_outputs;
    public int munOutputs() { return _n_outputs; }
    private native int mdb_device_get_num_outputs(long p);

    int _n_links_in;
    public int numLinksIn() { return _n_links_in; }
    private native int mdb_device_get_num_links_in(long p);

    int _n_links_out;
    public int numLinksOut() { return _n_links_out; }
    private native int mdb_device_get_num_links_out(long p);

    int _n_connections_in;
    public int numConnectionsIn() { return _n_connections_in; }
    private native int mdb_device_get_num_connections_in(long p);

    int _n_connections_out;
    public int numConnectionsOut() { return _n_connections_out; }
    private native int mdb_device_get_num_connections_out(long p);

    int _version;
    public int version() { return _version; }
    private native int mdb_device_get_version(long p);

    public TimeTag synced() {
        return mdb_device_get_synced(_devprops);
    }
    private native TimeTag mdb_device_get_synced(long p);

    public PropertyValue property(String property) {
        return mdb_device_property_lookup(_devprops, property);
    }
    private native PropertyValue mdb_device_property_lookup(
        long p, String property);

    private long _devprops;
}
