
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

    public Connection(long conprops) {
        _conprops = conprops;

        srcName = mdb_connection_get_src_name(_conprops);
        destName = mdb_connection_get_dest_name(_conprops);

        srcType = mdb_connection_get_src_type(_conprops);
        destType = mdb_connection_get_dest_type(_conprops);

        srcLength = mdb_connection_get_src_length(_conprops);
        destLength = mdb_connection_get_dest_length(_conprops);

        boundMin = mdb_connection_get_bound_min(_conprops);
        boundMax = mdb_connection_get_bound_max(_conprops);

        srcMin = mdb_connection_get_src_min(_conprops);
        srcMax = mdb_connection_get_src_max(_conprops);
        destMin = mdb_connection_get_dest_min(_conprops);
        destMax = mdb_connection_get_dest_max(_conprops);

        mode = mdb_connection_get_mode(_conprops);
        expression = mdb_connection_get_expression(_conprops);
        sendAsInstance = mdb_connection_get_send_as_instance(_conprops);

        numScopes = mdb_connection_get_num_scopes(_conprops);
        scopeNames = mdb_connection_get_scope_names(_conprops);
    }

    public Connection(String _srcName, String _destName) {
        srcName = _srcName;
        destName = _destName;
        srcType = 0;
        destType = 0;
        srcLength = -1;
        destLength = -1;
        boundMin = -1;
        boundMax = -1;
        srcMin = null;
        srcMax = null;
        destMin = null;
        destMax = null;
        mode = -1;
        expression = null;
        sendAsInstance = 0;
        numScopes = 0;
        scopeNames = null;
    }

    public Connection() {
        this(null, null);
    }

    public String srcName;
    private native String mdb_connection_get_src_name(long p);

    public String destName;
    private native String mdb_connection_get_dest_name(long p);

    public char srcType;
    private native char mdb_connection_get_src_type(long p);

    public char destType;
    private native char mdb_connection_get_dest_type(long p);

    public int srcLength;
    private native int mdb_connection_get_src_length(long p);

    public int destLength;
    private native int mdb_connection_get_dest_length(long p);

    public int boundMin;
    private native int mdb_connection_get_bound_min(long p);

    public int boundMax;
    private native int mdb_connection_get_bound_max(long p);

    public PropertyValue srcMin;
    private native PropertyValue mdb_connection_get_src_min(long p);

    public PropertyValue srcMax;
    private native PropertyValue mdb_connection_get_src_max(long p);

    public PropertyValue destMin;
    private native PropertyValue mdb_connection_get_dest_min(long p);

    public PropertyValue destMax;
    private native PropertyValue mdb_connection_get_dest_max(long p);

    public int mode;
    private native int mdb_connection_get_mode(long p);

    public String expression;
    private native String mdb_connection_get_expression(long p);

    public int sendAsInstance;
    private native int mdb_connection_get_send_as_instance(long p);

    public int numScopes;
    private native int mdb_connection_get_num_scopes(long p);

    public PropertyValue scopeNames;
    private native PropertyValue mdb_connection_get_scope_names(long p);

    public PropertyValue property(String property) {
        return mapper_db_connection_property_lookup(_conprops, property);
    }
    private native PropertyValue mapper_db_connection_property_lookup(
        long p, String property);

    private long _conprops;
}
