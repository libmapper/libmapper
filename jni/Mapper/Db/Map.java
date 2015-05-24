
package Mapper.Db;

import Mapper.PropertyValue;

public class Map
{
    /*! Describes what happens when the range boundaries are exceeded. */
    public static final int BA_UNDEFINED = 0;
    public static final int BA_NONE      = 1;
    public static final int BA_MUTE      = 2;
    public static final int BA_CLAMP     = 3;
    public static final int BA_FOLD      = 4;
    public static final int BA_WRAP      = 5;

    /*! Describes the mapping mode. */
    public static final int MO_UNDEFINED  = 0;
    public static final int MO_RAW        = 1;
    public static final int MO_LINEAR     = 2;
    public static final int MO_EXPRESSION = 3;

    /*! Describes the voice-stealing mode for instances. */
    public static final int IN_UNDEFINED    = 0;
    public static final int IN_STEAL_OLDEST = 1;
    public static final int IN_STEAL_NEWEST = 2;

    /*! Describes the directionality of map slots. */
    public static final int DI_OUTGOING = 1;
    public static final int DI_INCOMING = 2;
    public static final int DI_BOTH     = 3;

    public class Slot {
        private Slot(long s) {
            _slotprops = s;

            boundMin = mdb_map_slot_get_bound_min(_slotprops);
            boundMax = mdb_map_slot_get_bound_max(_slotprops);
            causeUpdate = mdb_map_slot_get_cause_update(_slotprops);
            direction = mdb_map_slot_get_direction(_slotprops);
            length = mdb_map_slot_get_length(_slotprops);
            maximum = mdb_map_slot_get_max(_slotprops);
            minimum = mdb_map_slot_get_min(_slotprops);
            sendAsInstance = mdb_map_slot_get_send_as_instance(_slotprops);
            type = mdb_map_slot_get_type(_slotprops);
        }
        private Slot() {
            boundMin = -1;
            boundMax = -1;
            causeUpdate = -1;
            direction = 0;
            length = -1;
            maximum = null;
            minimum = null;
            sendAsInstance = -1;
            type = 0;
        }

        public int boundMin;
        private native int mdb_map_slot_get_bound_min(long p);

        public int boundMax;
        private native int mdb_map_slot_get_bound_max(long p);

        public int causeUpdate;
        private native int mdb_map_slot_get_cause_update(long p);

        public int direction;
        private native int mdb_map_slot_get_direction(long p);

        public int length;
        private native int mdb_map_slot_get_length(long p);

        public PropertyValue maximum;
        private native PropertyValue mdb_map_slot_get_max(long p);

        public PropertyValue minimum;
        private native PropertyValue mdb_map_slot_get_min(long p);

        public int sendAsInstance;
        private native int mdb_map_slot_get_send_as_instance(long p);

        public Signal signal() {
            return new Signal(mdb_map_slot_get_signal(_slotprops));
        }
        private native long mdb_map_slot_get_signal(long p);

        public char type;
        private native char mdb_map_slot_get_type(long p);

        private long _slotprops;
    };

    public Map(long mapprops) {
        _mapprops = mapprops;

        expression = mdb_map_get_expression(_mapprops);
        id = mdb_map_get_id(_mapprops);
        mode = mdb_map_get_mode(_mapprops);
        muted = mdb_map_get_muted(_mapprops);
        numScopes = mdb_map_get_num_scopes(_mapprops);
        numSources = mdb_map_get_num_sources(_mapprops);
        scopeNames = mdb_map_get_scope_names(_mapprops);

        sources = new Slot[numSources];
        for (int i = 0; i < numSources; i++)
            sources[i] = new Slot(mdb_map_get_source_ptr(_mapprops, i));
        source = sources[0];
        destination = new Slot(mdb_map_get_dest_ptr(_mapprops));
    }

    public Map(int _numSources) {
        expression = null;
        id = -1;
        mode = -1;
        muted = -1;
        numScopes = 0;
        numSources = _numSources;
        scopeNames = null;

        sources = new Slot[numSources];
        for (int i = 0; i < numSources; i++)
            sources[i] = new Slot();
        source = sources[0];
        destination = new Slot();
    }

    public Map() {
        this(1);
    }

    public String expression;
    private native String mdb_map_get_expression(long p);

    public int id;
    private native int mdb_map_get_id(long p);

    public int mode;
    private native int mdb_map_get_mode(long p);

    public int muted;
    private native int mdb_map_get_muted(long p);

    public int numScopes;
    private native int mdb_map_get_num_scopes(long p);

    public int numSources;
    private native int mdb_map_get_num_sources(long p);

    public PropertyValue scopeNames;
    private native PropertyValue mdb_map_get_scope_names(long p);

    public PropertyValue property(String property) {
        return mapper_db_map_property_lookup(_mapprops, property);
    }
    private native PropertyValue mapper_db_map_property_lookup(
        long p, String property);

    private native long mdb_map_get_source_ptr(long p, int i);
    private native long mdb_map_get_dest_ptr(long p);

    private Slot getSource(int index)
    {
        if (index > numSources)
            return null;
        return sources[index];
    }
    private Slot getDest()
    {
        return destination;
    }

    public Slot[] sources;
    public Slot source;
    public Slot destination;

    private long _mapprops;
}
