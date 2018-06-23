
package mapper;

import mapper.NativeLib;
import mapper.Property;
import java.util.Iterator;
import java.util.Map;

public abstract class AbstractObject<T extends AbstractObject<T>>
{
    /* constructor */
    public AbstractObject(long obj) { _obj = obj; }

    /* free */
    private native void mapperObjectFree(long obj);
    public void free() {
        if (_obj != 0)
            mapperObjectFree(_obj);
        _obj = 0;
    }

    /* self */
    abstract T self();

    /* graph */
    private native long graph(long obj);
    public mapper.Graph graph() {
        return new mapper.Graph(graph(_obj));
    }

    /* properties */
    public native int numProperties();

    public native java.lang.Object getProperty(String propName);
    public native Map.Entry getProperty(int index);
    public Map.Entry getProperty(Property propID) {
        return getProperty(propID.value());
    }

    private native void mapperSetProperty(long obj, int prop, String name,
                                          java.lang.Object value);
//    private native void setProperty(long obj, int prop, String name, Enum<?> value);
//    private native void setProperty(long obj, int prop, String name, boolean value);
//    private native void setProperty(long obj, int prop, String name, int value);
//    private native void setProperty(long obj, int prop, String name, long value);
//    private native void setProperty(long obj, int prop, String name, float value);
//    private native void setProperty(long obj, int prop, String name, double value);
//    private native void setProperty(long obj, int prop, String name, String value);
//    private native void setProperty(long obj, int prop, String name, boolean[] value);
//    private native void setProperty(long obj, int prop, String name, int[] value);
//    private native void setProperty(long obj, int prop, String name, long[] value);
//    private native void setProperty(long obj, int prop, String name, float[] value);
//    private native void setProperty(long obj, int prop, String name, double[] value);
//    private native void setProperty(long obj, int prop, String name, String[] value);

    // TODO: also need arrays, list, etc - can use generics?

    public T setProperty(String propName, java.lang.Object propValue) {
        mapperSetProperty(_obj, 0, propName, propValue);
        return self();
    }

    public T setProperty(Property propID, java.lang.Object propValue) {
        mapperSetProperty(_obj, propID.value(), null, propValue);
        return self();
    }

//    public T setProperty(String name, Enum<?> e) {
//        setObjectProperty(_obj, 0, name, e);
//        return self();
//    }
//    public T setProperty(Property prop, Enum<?> e) {
//        setObjectProperty(_obj, prop.value(), null, e);
//        return self();
//    }
//    public T setProperty(String name, boolean value) {
//        setObjectProperty(_obj, 0, name, value);
//        return self();
//    }
//    public T setProperty(Property prop, boolean value) {
//        setObjectProperty(_obj, prop.value(), null, value);
//        return self();
//    }
//    public T setProperty(String name, int value) {
//        setObjectProperty(_obj, 0, name, value);
//        return self();
//    }
//    public T setProperty(Property prop, int value) {
//        setObjectProperty(_obj, prop.value(), null, value);
//        return self();
//    }
//    public T setProperty(String name, float value) {
//        setObjectProperty(_obj, 0, name, value);
//        return self();
//    }
//    public T setProperty(Property prop, float value) {
//        setObjectProperty(_obj, prop.value(), null, value);
//        return self();
//    }
//    public T setProperty(String name, double value) {
//        setObjectProperty(_obj, 0, name, value);
//        return self();
//    }
//    public T setProperty(Property prop, double value) {
//        setObjectProperty(_obj, prop.value(), null, value);
//        return self();
//    }
//    public T setProperty(String name, String value) {
//        setObjectProperty(_obj, 0, name, value);
//        return self();
//    }
//    public T setProperty(Property prop, String value) {
//        setObjectProperty(_obj, prop.value(), null, value);
//        return self();
//    }
//    public T setProperty(String name, boolean[] value) {
//        setObjectProperty(_obj, 0, name, value);
//        return self();
//    }
//    public T setProperty(Property prop, boolean[] value) {
//        setObjectProperty(_obj, prop.value(), null, value);
//        return self();
//    }
//    public T setProperty(String name, int[] value) {
//        setObjectProperty(_obj, 0, name, value);
//        return self();
//    }
//    public T setProperty(Property prop, int[] value) {
//        setObjectProperty(_obj, prop.value(), null, value);
//        return self();
//    }
//    public T setProperty(String name, float[] value) {
//        setObjectProperty(_obj, 0, name, value);
//        return self();
//    }
//    public T setProperty(Property prop, float[] value) {
//        setObjectProperty(_obj, prop.value(), null, value);
//        return self();
//    }
//    public T setProperty(String name, double[] value) {
//        setObjectProperty(_obj, 0, name, value);
//        return self();
//    }
//    public T setProperty(Property prop, double[] value) {
//        setObjectProperty(_obj, prop.value(), null, value);
//        return self();
//    }
//    public T setProperty(String name, String[] value) {
//        setObjectProperty(_obj, 0, name, value);
//        return self();
//    }
//    public T setProperty(Property prop, String[] value) {
//        setObjectProperty(_obj, prop.value(), null, value);
//        return self();
//    }

    private native void removeProperty(int propID, String name);
    public T removeProperty(Property propID) {
        removeProperty(propID.value(), null);
        return self();
    }
    public T removeProperty(String propName) {
        removeProperty(0, propName);
        return self();
    }

    private native void mapperPush(long obj);
    public T push() {
        mapperPush(_obj);
        return self();
    }

    // Note: this is _not_ guaranteed to run, the user should still
    // call free() explicitly when the object is no longer needed.
    protected void finalize() throws Throwable {
        try {
            free();
        } finally {
            super.finalize();
        }
    }

    protected long _obj;
    public boolean valid() {
        return _obj != 0;
    }

    static {
        System.loadLibrary(NativeLib.name);
    }
}
