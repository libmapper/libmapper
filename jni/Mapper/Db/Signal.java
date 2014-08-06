
package Mapper.Db;

import Mapper.PropertyValue;

public class Signal
{
    public Signal(long sigprops) {
        _sigprops = sigprops;

        _name = mdb_signal_get_name(_sigprops);
        _device_name = mdb_signal_get_device_name(_sigprops);

        _is_output = mdb_signal_get_is_output(_sigprops);
        _type = mdb_signal_get_type(_sigprops);
        _length = mdb_signal_get_length(_sigprops);
        _unit = mdb_signal_get_unit(_sigprops);
        _minimum = mdb_signal_get_minimum(_sigprops);
        _maximum = mdb_signal_get_maximum(_sigprops);
        _rate = mdb_signal_get_rate(_sigprops);
    }

    private String _name;
    public String name() { return _name; }
    private native String mdb_signal_get_name(long p);

    private String _device_name;
    public String deviceName() { return _device_name; }
    private native String mdb_signal_get_device_name(long p);

	boolean _is_output;
    public boolean isOutput() { return _is_output; }
    private native boolean mdb_signal_get_is_output(long p);

    char _type;
    public char type() { return _type; }
    private native char mdb_signal_get_type(long p);

    int _length;
    public int length() { return _length; }
    private native int mdb_signal_get_length(long p);

    private String _unit;
    public String unit() { return _unit; }
    private native String mdb_signal_get_unit(long p);

    PropertyValue _minimum;
    public PropertyValue minimum() {
        _minimum = mdb_signal_get_minimum(_sigprops);
        return _minimum;
    }
    private native PropertyValue mdb_signal_get_minimum(long p);

    PropertyValue _maximum;
    public PropertyValue maximum() {
        _maximum = mdb_signal_get_maximum(_sigprops);
        return _maximum;
    }
    private native PropertyValue mdb_signal_get_maximum(long p);

    double _rate;
    public double rate() { return _rate; }
    private native double mdb_signal_get_rate(long p);

    public PropertyValue property(String property) {
        return mdb_signal_property_lookup(_sigprops, property);
    }
    private native PropertyValue mdb_signal_property_lookup(
        long p, String property);

    private long _sigprops;
}
