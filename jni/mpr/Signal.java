
package mpr;

import mpr.Map;
import mpr.signal.*;
import mpr.Device;
import java.util.Set;

public class Signal extends AbstractObject
{
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
    private native void mprSignalSetCB(long sig, Listener l, int flags);
    public Signal setListener(Listener l, Event event) {
        mprSignalSetCB(_obj, l, event.value());
        return this;
    }
    public Signal setListener(Listener l, Set<Event> events) {
        int flags = 0;
        for (Event e : Event.values()) {
            if (events.contains(e))
                flags |= e.value();
        }
        mprSignalSetCB(_obj, l, flags);
        return this;
    }
    public Signal setListener(Listener l) {
        mprSignalSetCB(_obj, l, Event.UPDATE.value());
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

    /* update signal or instance */
    private native Signal updateInstance(long id, int i, Time t);
    private native Signal updateInstance(long id, float f, Time t);
    private native Signal updateInstance(long id, double d, Time t);
    private native Signal updateInstance(long id, int[] i, Time t);
    private native Signal updateInstance(long id, float[] f, Time t);
    private native Signal updateInstance(long id, double[] d, Time t);

    public Signal setValue(int i) {
        return updateInstance(0, i, null);
    }
    public Signal setValue(float f) {
        return updateInstance(0, f, null);
    }
    public Signal setValue(double d) {
        return updateInstance(0, d, null);
    }
    public Signal setValue(int[] i) {
        return updateInstance(0, i, null);
    }
    public Signal setValue(float[] d) {
        return updateInstance(0, d, null);
    }
    public Signal setValue(double[] d) {
        return updateInstance(0, d, null);
    }

    public Signal setValue(int i, Time t) {
        return updateInstance(0, i, t);
    }
    public Signal setValue(float f, Time t) {
        return updateInstance(0, f, t);
    }
    public Signal setValue(double d, Time t) {
        return updateInstance(0, d, t);
    }
    public Signal setValue(int[] i, Time t) {
        return updateInstance(0, i, t);
    }
    public Signal setValue(float[] f, Time t) {
        return updateInstance(0, f, t);
    }
    public Signal setValue(double[] d, Time t) {
        return updateInstance(0, d, t);
    }

    /* signal or instance value */
    public native boolean hasValue();

    public native int intValue(long id);
    public int intValue() { return intValue(0); }
    public native float floatValue(long id);
    public float floatValue() { return floatValue(0); }
    public native double doubleValue(long id);
    public double doubleValue() { return doubleValue(0); }

    public native int[] intValues(long id);
    public int[] intValues() { return intValues(0); }
    public native float[] floatValues(long id);
    public float[] floatValues() { return floatValues(0); }
    public native double[] doubleValues(long id);
    public double[] doubleValues() { return doubleValues(0); }

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
        public native void release(Time t);
        public void release() {
            release(null);
        }

        /* remove instances */
        private native void mprFreeInstance(long sig, long id, Time t);
        public void free(Time t) {
            mprFreeInstance(_sigptr, _id, t);
        }
        public void free() {
            mprFreeInstance(_sigptr, _id, null);
        }

        public Signal signal() { return _sigobj; }

        public native int isActive();
        public long id() { return _id; }

        /* update */
        public Instance setValue(int i, Time t) {
            updateInstance(_id, i, t);
            return this;
        }
        public Instance setValue(float f, Time t) {
            updateInstance(_id, f, t);
            return this;
        }
        public Instance setValue(double d, Time t) {
            updateInstance(_id, d, t);
            return this;
        }
        public Instance setValue(int[] i, Time t) {
            updateInstance(_id, i, t);
            return this;
        }
        public Instance setValue(float[] f, Time t) {
            updateInstance(_id, f, t);
            return this;
        }
        public Instance setValue(double[] d, Time t) {
            updateInstance(_id, d, t);
            return this;
        }

        public Instance setValue(int i) {
            updateInstance(_id, i, null);
            return this;
        }
        public Instance setValue(float f) {
            updateInstance(_id, f, null);
            return this;
        }
        public Instance setValue(double d) {
            updateInstance(_id, d, null);
            return this;
        }
        public Instance setValue(int[] i) {
            updateInstance(_id, i, null);
            return this;
        }
        public Instance setValue(float[] f) {
            updateInstance(_id, f, null);
            return this;
        }
        public Instance setValue(double[] d) {
            updateInstance(_id, d, null);
            return this;
        }

        /* value */
        public int intValue() { return _sigobj.intValue(_id); }
        public float floatValue() { return _sigobj.floatValue(_id); }
        public double doubleValue() { return _sigobj.doubleValue(_id); }
        public int[] intValues() { return _sigobj.intValues(_id); }
        public float[] floatValues() { return _sigobj.floatValues(_id); }
        public double[] doubleValues() { return _sigobj.doubleValues(_id); }

        /* userObject */
        public native java.lang.Object userReference();
        public native Instance setUserReference(java.lang.Object obj);

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
