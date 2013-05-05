
package Mapper.Db;

import Mapper.Device;
import Mapper.PropertyValue;

public class Signal
{
    public Signal(long sigprops, Device.Signal s) {
        _sigprops = sigprops;
        _signal = s;

        _name = msig_db_signal_get_name(_sigprops);
        _device_name = msig_db_signal_get_device_name(_sigprops);

        _is_output = msig_db_signal_get_is_output(_sigprops);
        _type = msig_db_signal_get_type(_sigprops);
        _length = msig_db_signal_get_length(_sigprops);
        _unit = msig_db_signal_get_unit(_sigprops);
        _minimum = msig_db_signal_get_minimum(_sigprops);
        _maximum = msig_db_signal_get_maximum(_sigprops);
        _rate = msig_db_signal_get_rate(_sigprops);
    }

    private String _name;
    public String name() { return _name; }
    public void set_name(String _name) {
        checkParents();
        msig_db_signal_set_name(_sigprops, _name);
        _name = msig_db_signal_get_name(_sigprops);
    }
    private native void msig_db_signal_set_name(long p, String name);
    private native String msig_db_signal_get_name(long p);

    private String _device_name;
    public String device_name() { return _device_name; }
    private native String msig_db_signal_get_device_name(long p);

	boolean _is_output;
    public boolean is_output() { return _is_output; }
    private native boolean msig_db_signal_get_is_output(long p);

    char _type;
    public char type() { return _type; }
    private native char msig_db_signal_get_type(long p);

    int _length;
    public int length() { return _length; }
    private native int msig_db_signal_get_length(long p);

    private String _unit;
    public String unit() { return _unit; }
    public void set_unit(String _unit) {
        checkParents();
        msig_db_signal_set_unit(_sigprops, _unit);
        _unit = msig_db_signal_get_unit(_sigprops);
    }
    private native void msig_db_signal_set_unit(long p, String unit);
    private native String msig_db_signal_get_unit(long p);

    Double _minimum;
    public Double minimum() { return _minimum; }
    private native Double msig_db_signal_get_minimum(long p);

    Double _maximum;
    public Double maximum() { return _maximum; }
    private native Double msig_db_signal_get_maximum(long p);

    double _rate;
    public double rate() { return _rate; }
    private native double msig_db_signal_get_rate(long p);

    public PropertyValue property_lookup(String property) {
        return mapper_db_signal_property_lookup(_sigprops, property);
    }
    private native PropertyValue mapper_db_signal_property_lookup(
        long p, String property);

    private void checkParents() {
        if (!_signal.valid())
            throw new NullPointerException(
                "Cannot set property for a Signal object that "
                +"is associated with an invalid Device");
    }

    private long _sigprops;
    private Device.Signal _signal;
}
