
package mapper;

import mapper.Map;
import mapper.signal.*;
import mapper.Device;
import java.util.Set;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

public class Signal extends AbstractObject
{
    /* constructors */
    public Signal(long sig) {
        super(sig);
    }

    /* self */
    @Override
    public Signal self() {
        return this;
    }

    /* device */
    public native Device device();

    /* callbacks */
    private native void mapperSignalSetCB(long sig, Listener l, String methodSig, int flags);
    private void _setListener(Listener l, int flags) {
        if (l == null) {
            mapperSignalSetCB(_obj, null, null, 0);
            return;
        }

        // try to check type of listener
        String[] instanceName = l.toString().split("@", 0);
        for (Method method : l.getClass().getMethods()) {
            if ((method.getModifiers() & Modifier.STATIC) != 0)
                continue;
            String methodName = method.toString();
            if (methodName.startsWith("public void "+instanceName[0]+".")) {
                mapperSignalSetCB(_obj, l, methodName, flags);
                return;
            }
        }
        System.out.println("Error: no match for listener.");
    }
    public Signal setListener(Listener l, Event event) {
        _setListener(l, event.value());
        return this;
    }
    public Signal setListener(Listener l, Set<Event> events) {
        int flags = 0;
        for (Event e : Event.values()) {
            if (events.contains(e))
                flags |= e.value();
        }
        _setListener(l, flags);
        return this;
    }
    public Signal setListener(Listener l) {
        _setListener(l, Event.UPDATE.value());
        return this;
    }
    public native Listener listener();

    private native void mapperSignalReserveInstances(long sig, int num, long[] ids);
    public Signal reserveInstances(int num) {
        mapperSignalReserveInstances(_obj, num, null);
        return this;
    }
    public Signal reserveInstances(long[] ids) {
        mapperSignalReserveInstances(_obj, 0, ids);
        return this;
    }

    public Instance instance(long id) {
        return new Instance(id);
    }
    public Instance instance() {
        return new Instance();
    }
    public Instance instance(java.lang.Object obj) {
        return new Instance(obj);
    }

    /* instance management */
    public native Instance oldestActiveInstance();
    public native Instance newestActiveInstance();

    public native int numActiveInstances();
    public native int numReservedInstances();

    /* set value */
    public native Signal setValue(long id, Object value);
    public Signal setValue(Object value) {
        return setValue(0, value);
    }

    /* get value */
    public native boolean hasValue(long id);
    public boolean hasValue() { return hasValue(0); }

    public native Object getValue(long id);
    public Object getValue() { return getValue(0); }

    public native Signal releaseInstance(long id);
    public native Signal removeInstance(long id);

    public class Instance extends AbstractObject
    {
        /* constructors */
        private native long mapperInstance(boolean hasId, long id, java.lang.Object obj);
        private Instance(long id) {
            super(Signal.this._obj);
            _id = mapperInstance(true, id, null);
        }
        private Instance(java.lang.Object obj) {
            super(Signal.this._obj);
            _id = mapperInstance(false, 0, obj);
        }
        private Instance() {
            super(Signal.this._obj);
            _id = mapperInstance(false, 0, null);
        }

        /* self */
        @Override
        public Instance self() {
            return this;
        }

        /* release */
        public void release() { Signal.this.releaseInstance(_id); }

        /* remove instances */
        public void free() { Signal.this.removeInstance(_id); }

        public Signal signal() { return Signal.this; }

        public native int isActive();
        public long id() { return _id; }

        public boolean hasValue() { return Signal.this.hasValue(_id); }

        /* update */
        public Instance setValue(Object value) {
            Signal.this.setValue(_id, value);
            return this;
        }

        /* value */
        public Object getValue() { return Signal.this.getValue(_id); }

        /* userObject */
        public native java.lang.Object userReference();
        public native Instance setUserReference(java.lang.Object obj);

        /* properties */
        // TODO: filter for instance-specific properties like id, value, time

        private long _id;
    }

    /* retrieve associated maps */
    private native long maps(long sig, int dir);
    public mapper.List<mapper.Map> maps(Direction dir)
        { return new mapper.List<mapper.Map>(maps(_obj, dir.value())); }
    public mapper.List<mapper.Map> maps()
        { return maps(Direction.ANY); }
}
