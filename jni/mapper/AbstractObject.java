
package mapper;

import mapper.NativeLib;
import mapper.Property;

public abstract class AbstractObject<T extends AbstractObject<T>>
{
    /* constructor */
    public AbstractObject(long obj) { _obj = obj; }

    /* self */
    abstract T self();

    /* graph */
    private native long graph(long obj);
    public mapper.Graph graph() {
        return new mapper.Graph(graph(_obj));
    }

    private native void _push(long obj);

    /* properties */
    public class Properties
    {
        /* constructor */
        public Properties(AbstractObject obj) { _obj = obj._obj; }

        public void clear()
            { throw new UnsupportedOperationException(); }

        private native boolean _containsKey(long obj, int idx, String key);
        public boolean containsKey(Object key)
        {
            if (key instanceof Integer)
                return _containsKey(_obj, (int)key, null);
            else if (key instanceof String)
                return _containsKey(_obj, 0, (String)key);
            else if (key instanceof mapper.Property)
                return _containsKey(_obj, ((mapper.Property)key).value(), null);
            else
                return false;
        }

        private native boolean _containsValue(long obj, Object value);
        public boolean containsValue(Object value)
            { return _containsValue(_obj, value); }

        public class Entry
        {
            private native Entry _set(long obj, int idx, String key);

            public String getKey()
                { return _key; }
            public mapper.Property getProperty()
                { return mapper.Property.values()[_id]; }
            public Object getValue()
                { return _value; }
            public String toString()
                { return _key + ": " + _value.toString(); }

            private int _id;
            private String _key;
            private Object _value;
        }

        // TODO: implement iterator

        public Entry getEntry(Object key)
        {
            if (key instanceof Integer) {
                Entry e = new Entry();
                return e._set(_obj, (int)key, null);
            }
            else if (key instanceof String) {
                Entry e = new Entry();
                return e._set(_obj, 0, (String)key);
            }
            else if (key instanceof mapper.Property) {
                Entry e = new Entry();
                return e._set(_obj, ((mapper.Property)key).value(), null);
            }
            else
                return null;
        }

        private native Object _get(long obj, int idx, String key);
        public Object get(Object key)
        {
            if (key instanceof Integer)
                return _get(_obj, (int)key, null);
            else if (key instanceof String)
                return _get(_obj, 0, (String)key);
            else if (key instanceof mapper.Property)
                return _get(_obj, ((mapper.Property)key).value(), null);
            else
                return null;
        }

        public boolean isEmpty()
            { return false; }

        private native Object _put(long obj, int idx, String key, Object value);
        public Object put(Object key, Object value)
        {
            // translate some enum objects to their int form
            if (value instanceof mapper.Direction)
                value = ((mapper.Direction)value).value();
            else if (value instanceof mapper.map.Location)
                value = ((mapper.map.Location)value).value();
            else if (value instanceof mapper.signal.StealMode)
                value = ((mapper.signal.StealMode)value).value();
            else if (value instanceof mapper.Type)
                value = ((mapper.Type)value).value();

            if (key instanceof Integer)
                return _put(_obj, (int)key, null, value);
            else if (key instanceof String)
                return _put(_obj, 0, (String)key, value);
            else if (key instanceof mapper.Property)
                return _put(_obj, ((mapper.Property)key).value(), null, value);
            else
                return null;
        }

        private native Object _remove(long obj, int idx, String key);
        public Object remove(Object key)
        {
            if (key instanceof Integer)
                return _remove(_obj, (int)key, null);
            else if (key instanceof String)
                return _remove(_obj, 0, (String)key);
            else if (key instanceof mapper.Property)
                return _remove(_obj, ((mapper.Property)key).value(), null);
            else
                return null;
        }

        private native int _size(long obj);
        public int size()
            { return _size(_obj); }
    }

    public Properties properties()
        { return new Properties(this); }

    public T push() {
        _push(_obj);
        return self();
    }

    // Note: this is _not_ guaranteed to run, the user should still
    // call free() explicitly when the object is no longer needed.
//    protected void finalize() throws Throwable {
//        try {
//            free();
//        } finally {
//            super.finalize();
//        }
//    }

    protected long _obj;
    public boolean valid() {
        return _obj != 0;
    }

    static {
        System.loadLibrary(NativeLib.name);
    }
}
