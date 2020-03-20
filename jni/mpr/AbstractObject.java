
package mpr;

import mpr.NativeLib;
import mpr.Property;
import java.util.Iterator;
import java.util.AbstractMap;
import java.util.Map.*;
import java.util.Set;

public abstract class AbstractObject<T extends AbstractObject<T>>
{
    /* constructor */
    public AbstractObject(long obj) { _obj = obj; }

    /* free */
    private native void mprObjectFree(long obj);
    public void free() {
        if (_obj != 0)
            mprObjectFree(_obj);
        _obj = 0;
    }

    /* self */
    abstract T self();

    /* graph */
    private native long graph(long obj);
    public mpr.Graph graph() {
        return new mpr.Graph(graph(_obj));
    }

    private native void _push(long obj);

    /* properties */
    public class Properties extends AbstractMap<Object,Object>
    {
        /* constructor */
        public Properties(AbstractObject obj) { _obj = obj._obj; }

        private native int _count(long obj);
        public int count()
            { return _count(_obj); }

        private native Entry<Object, Object> _get(long obj, int id, String key);
        public Entry<Object, Object> get(Property propID)
            { return _get(_obj, propID.value(), null); }
        public Entry<Object, Object> get(int index)
            { return _get(_obj, index, null); }
        public Entry<Object, Object> get(String key)
            { return _get(_obj, 0, key); }

        private native Set<Object> _keySet(long obj);
        public Set<Object> keySet()
            { return _keySet(_obj); }

        private native Set<Entry<Object, Object>> _entrySet(long obj);
        public Set<Entry<Object, Object>> entrySet()
            { return _entrySet(_obj); }

        private native void _put(long obj, int id, String key, Object value);
        public Properties put(Property propID, Object value)
            { _put(_obj, propID.value(), null, value); return this; }
        public Properties put(String key, Object value)
            { _put(_obj, 0, key, value); return this; }

        private native void _remove(long obj, int id, String key);
        public Properties remove(Property propID)
            { _remove(_obj, propID.value(), null); return this; }
        public Properties remove(int index)
            { _remove(_obj, index, null); return this; }
        public Properties remove(String key)
            { _remove(_obj, 0, key); return this; }

//        public Properties push() {
//            _push(_obj);
//            return this;
//        }
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
