
package mapper;

import mapper.graph.*;
//import mapper.map.*;
//import mapper.signal.*;
import java.util.*;
//import java.util.HashSet;

public class Graph extends mapper.AbstractObject
{
    /* constructor */
    private native long mapperGraphNew(int flags);
    public Graph(Set<Type> types) {
        super(0);
        int flags = 0;
        for (Type t : Type.values()) {
            if (types.contains(t))
                flags |= t.value();
        }
        _obj = mapperGraphNew(flags);
        _owned = true;
    }
    public Graph(Type type) {
        super(0);
        _obj = mapperGraphNew(type.value());
        _listeners = new HashSet();
        _owned = true;
    }
    public Graph() {
        super(0);
        _obj = mapperGraphNew(Type.OBJECT.value());
        _listeners = new HashSet();
        _owned = true;
    }
    public Graph(long graph) {
        super(graph);
        _listeners = new HashSet();
    }

    /* free */
    private native void mapperGraphFree(long graph);
    public void free() {
        if (_obj != 0 && _owned)
            mapperGraphFree(_obj);
        _obj = 0;
    }

    /* self */
    @Override
    public Graph self() {
        return this;
    }

    /* interface */
    public native String getInterface();
    public native Graph setInterface(String iface);

    /* multicast address */
    public native String getAddress();
    public native Graph setAddress(String group, int port);

    /* poll */
    public native Graph poll(int timeout);
    public Graph poll() { return poll(0); }

    /* subscriptions */
    private native void mapperGraphSubscribe(long graph, mapper.Device dev,
                                             int flags, int timeout);
    public Graph subscribe(mapper.Device dev, Type type, int lease) {
        mapperGraphSubscribe(_obj, dev, type.value(), lease);
        return this;
    }
    public Graph subscribe(mapper.Device dev, Type type) {
        return subscribe(dev, type, -1);
    }
    public Graph subscribe(mapper.Device dev, Set<Type> types, int lease) {
        int flags = 0;
        for (Type t : Type.values()) {
            if (types.contains(t))
                flags |= t.value();
        }
        mapperGraphSubscribe(_obj, dev, flags, lease);
        return this;
    }
    public Graph subscribe(mapper.Device dev, Set<Type> types) {
        return subscribe(dev, types, -1);
    }

    public native Graph unsubscribe(mapper.Device dev);

    // Listeners
    private native void addCallback(long graph, Listener l);
    public Graph addListener(Listener l) {
        addCallback(_obj, l);
        _listeners.add(l);
        return this;
    }

    private native void removeCallback(long graph, Listener l);
    public Graph removeListener(Listener l) {
        removeCallback(_obj, l);
        _listeners.remove(l);
        return this;
    }

    // devices
    private native long devices(long graph);
    public mapper.List<mapper.Device> devices()
        { return new mapper.List<mapper.Device>(devices(_obj)); }

    // signals
    private native long signals(long graph);
    public mapper.List<mapper.Signal> signals()
        { return new mapper.List<mapper.Signal>(signals(_obj)); }

    // maps
    private native long maps(long graph);
    public mapper.List<mapper.Map> maps()
        { return new mapper.List<mapper.Map>(maps(_obj)); }

    /* Note: this is _not_ guaranteed to run, the user should still call free()
     * explicitly when the graph is no longer needed. */
    protected void finalize() throws Throwable {
        try {
            free();
        } finally {
            super.finalize();
        }
    }

    private Set<Listener> _listeners;

    static {
        System.loadLibrary(NativeLib.name);
    }
}
