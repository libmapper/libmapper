
package Mapper.Db;

import Mapper.PropertyValue;

public class Connection
{
    /*! Describes what happens when the range boundaries are exceeded. */
    public static final int BA_NONE  = 0;
    public static final int BA_MUTE  = 1;
    public static final int BA_CLAMP = 2;
    public static final int BA_FOLD  = 3;
    public static final int BA_WRAP  = 4;
    
    /*! Describes the connection mode. */
    public static final int MO_UNDEFINED  = 0;
    public static final int MO_BYPASS     = 1;
    public static final int MO_LINEAR     = 2;
    public static final int MO_EXPRESSION = 3;
    public static final int MO_CALIBRATE  = 4;
    public static final int MO_REVERSE    = 5;
    
    /*! Describes the voice-stealing mode for instances. */
    public static final int IN_UNDEFINED    = 0;
    public static final int IN_STEAL_OLDEST = 1;
    public static final int IN_STEAL_NEWEST = 2;

    private Connection(long conprops) {
        _conprops = conprops;

        _src_name = mdb_connection_get_src_name(_conprops);
        _dest_name = mdb_connection_get_dest_name(_conprops);

        _src_type = mdb_connection_get_src_type(_conprops);
        _dest_type = mdb_connection_get_dest_type(_conprops);

        _src_length = mdb_connection_get_src_length(_conprops);
        _dest_length = mdb_connection_get_dest_length(_conprops);

        _bound_min = mdb_connection_get_bound_min(_conprops);
        _bound_max = mdb_connection_get_bound_max(_conprops);

        _src_min = mdb_connection_get_src_min(_conprops);
        _src_max = mdb_connection_get_src_max(_conprops);
        _dest_min = mdb_connection_get_dest_min(_conprops);
        _dest_max = mdb_connection_get_dest_max(_conprops);

        _range_known = mdb_connection_get_range_known(_conprops);
    }

    private String _src_name;
    public String srcName() { return _src_name; }
    private native String mdb_connection_get_src_name(long p);

    private String _dest_name;
    public String destName() { return _dest_name; }
    private native String mdb_connection_get_dest_name(long p);

    char _src_type;
    public char srcType() { return _src_type; }
    private native char mdb_connection_get_src_type(long p);

    char _dest_type;
    public char destType() { return _dest_type; }
    private native char mdb_connection_get_dest_type(long p);

    int _src_length;
    public int srcLength() { return _src_length; }
    private native int mdb_connection_get_src_length(long p);

    int _dest_length;
    public int destLength() { return _dest_length; }
    private native int mdb_connection_get_dest_length(long p);

    int _bound_min;
    public int boundMin() { return _bound_min; }
    private native int mdb_connection_get_bound_min(long p);

    int _bound_max;
    public int boundMax() { return _bound_max; }
    private native int mdb_connection_get_bound_max(long p);

    PropertyValue _src_min;
    public PropertyValue srcMinimum() {
        _src_min = mdb_connection_get_src_min(_conprops);
        return _src_min;
    }
    private native PropertyValue mdb_connection_get_src_min(long p);

    PropertyValue _src_max;
    public PropertyValue srcMaximum() {
        _src_max = mdb_connection_get_src_max(_conprops);
        return _src_max;
    }
    private native PropertyValue mdb_connection_get_src_max(long p);

    PropertyValue _dest_min;
    public PropertyValue destMinimum() {
        _dest_min = mdb_connection_get_dest_min(_conprops);
        return _src_min;
    }
    private native PropertyValue mdb_connection_get_dest_min(long p);

    PropertyValue _dest_max;
    public PropertyValue destMaximum() {
        _dest_max = mdb_connection_get_dest_max(_conprops);
        return _dest_max;
    }
    private native PropertyValue mdb_connection_get_dest_max(long p);

    int _range_known;
    public int rangeKnown() { return _range_known; }
    private native int mdb_connection_get_range_known(long p);

    public PropertyValue property(String property) {
        return mapper_db_connection_property_lookup(_conprops, property);
    }
    private native PropertyValue mapper_db_connection_property_lookup(
        long p, String property);

    private long _conprops;
}
