
package mpr;

import mpr.Map;
import mpr.signal.*;
import mpr.Device;
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
    private native void mprSignalSetCB(long sig, Listener l, String methodSig,
                                       int flags);
    private void _setListener(Listener l, int flags) {
        if (l == null) {
            mprSignalSetCB(_obj, null, null, 0);
            return;
        }

        // try to check type of listener
        String[] instanceName = l.toString().split("@", 0);
        for (Method method : l.getClass().getMethods()) {
            if ((method.getModifiers() & Modifier.STATIC) != 0)
                continue;
            String methodName = method.toString();
            if (methodName.startsWith("public void "+instanceName[0]+".")) {
                mprSignalSetCB(_obj, l, methodName, flags);
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

    private native void mprSignalReserveInstances(long sig, int num, long[] ids);
    public Signal reserveInstances(int num) {
        mprSignalReserveInstances(_obj, num, null);
        return this;
    }
    public Signal reserveInstances(long[] ids) {
        mprSignalReserveInstances(_obj, 0, ids);
        return this;
    }

    public Instance instance(long id) {
        return new Instance(this, id);
    }
    public Instance instance() {
        return new Instance(this);
    }
    public Instance instance(java.lang.Object obj) {
        return new Instance(this, obj);
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

    public class Instance
    {
        private native long mprInstance(boolean hasId, long id,
                                        java.lang.Object obj);
        private Instance(Signal sig, long id) {
            _sigobj = sig;
            _sigptr = sig._obj;
            _id = mprInstance(true, id, null);
        }
        private Instance(Signal sig, java.lang.Object obj) {
            _sigobj = sig;
            _sigptr = sig._obj;
            _id = mprInstance(false, 0, obj);
        }
        private Instance(Signal sig) {
            _sigobj = sig;
            _sigptr = sig._obj;
            _id = mprInstance(false, 0, null);
        }

        /* release */
        public native void release();

        /* remove instances */
        public native void free();

        public Signal signal() { return _sigobj; }

        public native int isActive();
        public long id() { return _id; }

        public boolean hasValue() { return _sigobj.hasValue(_id); }

        /* update */
        public Instance setValue(Object value) {
            _sigobj.setValue(_id, value);
            return this;
        }

        /* value */
        public Object getValue() { return _sigobj.getValue(_id); }

        /* userObject */
        public native java.lang.Object userReference();
        public native Instance setUserReference(java.lang.Object obj);

        /* properties */
        // TODO: filter for instance-specific properties like id, value, time
        public Signal.Properties properties()
            { return _sigobj.properties(); }

        private Signal _sigobj;
        private long _sigptr;
        private long _id;
    }

    /* retrieve associated maps */
    private native long maps(long sig, int dir);
    public mpr.List<mpr.Map> maps(Direction dir)
        { return new mpr.List<mpr.Map>(maps(_obj, dir.value())); }
    public mpr.List<mpr.Map> maps()
        { return maps(Direction.ANY); }
}
