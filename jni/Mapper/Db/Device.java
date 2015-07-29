
package Mapper.Db;

import Mapper.PropertyValue;
import Mapper.TimeTag;

public class Device
{
    public Device(long db, long devprops) {
        _db = db;
        _devprops = devprops;

        _name = mdb_device_get_name(_devprops);
        _ordinal = mdb_device_get_ordinal(_devprops);
        _host = mdb_device_get_host(_devprops);
        _port = mdb_device_get_port(_devprops);

        _n_inputs = mdb_device_get_num_inputs(_devprops);
        _n_outputs = mdb_device_get_num_outputs(_devprops);
        _n_maps_in = mdb_device_get_num_incoming_maps(_devprops);
        _n_maps_out = mdb_device_get_num_outgoing_maps(_devprops);
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

    int _n_maps_in;
    public int numIncomingMaps() { return _n_maps_in; }
    private native int mdb_device_get_num_incoming_maps(long p);

    int _n_maps_out;
    public int numOutgoingMaps() { return _n_maps_out; }
    private native int mdb_device_get_num_outgoing_maps(long p);

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

    // Signal
    public native Mapper.Db.Signal signalByName(String name);
    public native Mapper.Db.Signal inputByName(String name);
    public native Mapper.Db.Signal outputByName(String name);

    // Signals
    private native long mdb_device_signals(long db, long device, String pattern,
                                           int direction);
    public Mapper.Db.SignalCollection signals()
    {
        long _s = mdb_device_signals(_db, _devprops, null, 0);
        return new Mapper.Db.SignalCollection(_s);
    }
    public Mapper.Db.SignalCollection inputs()
    {
        long _s = mdb_device_signals(_db, _devprops, null, 1);
        return new Mapper.Db.SignalCollection(_s);
    }
    public Mapper.Db.SignalCollection outputs()
    {
        long _s = mdb_device_signals(_db, _devprops, null, 2);
        return new Mapper.Db.SignalCollection(_s);
    }
    public Mapper.Db.SignalCollection signalsByNameMatch(String pattern)
    {
        long _s = mdb_device_signals(_db, _devprops, pattern, 0);
        return new Mapper.Db.SignalCollection(_s);
    }
    public Mapper.Db.SignalCollection inputsByNameMatch(String pattern)
    {
        long _s = mdb_device_signals(_db, _devprops, pattern, 1);
        return new Mapper.Db.SignalCollection(_s);
    }
    public Mapper.Db.SignalCollection outputsByNameMatch(String pattern)
    {
        long _s = mdb_device_signals(_db, _devprops, pattern, 2);
        return new Mapper.Db.SignalCollection(_s);
    }

    // Maps
    private native long mdb_device_maps(long db, long device, int direction);
    public Mapper.Db.MapCollection maps()
    {
        long _m = mdb_device_maps(_db, _devprops, 0);
        return new Mapper.Db.MapCollection(_m);
    }
    public Mapper.Db.MapCollection incomingMaps()
    {
        long _m = mdb_device_maps(_db, _devprops, 1);
        return new Mapper.Db.MapCollection(_m);
    }
    public Mapper.Db.MapCollection outgoingMaps()
    {
        long _m = mdb_device_maps(_db, _devprops, 2);
        return new Mapper.Db.MapCollection(_m);
    }

    private long _db;
    private long _devprops;
}
