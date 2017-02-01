
package mapper;

import mapper.Map;
import mapper.map.*;
import mapper.signal.*;
import mapper.Device;
import mapper.Value;
import java.util.Iterator;
import java.util.Set;

public class Signal
{
    public Signal(long sig) {
        _sig = sig;
    }

    /* device */
    public native Device device();

    /* callbacks */
    private native void mapperSignalSetInstanceEventCB(InstanceEventListener l,
                                                       int flags);
    public Signal setInstanceEventListener(InstanceEventListener l,
                                           InstanceEvent event) {
        mapperSignalSetInstanceEventCB(l, event.value());
        return this;
    }
    public Signal setInstanceEventListener(InstanceEventListener l,
                                           Set<InstanceEvent> events) {
        int flags = 0;
        for (InstanceEvent e : InstanceEvent.values()) {
            if (events.contains(e))
                flags |= e.value();
        }
        mapperSignalSetInstanceEventCB(l, flags);
        return this;
    }
    public native Signal setUpdateListener(UpdateListener l);
    public native Signal setInstanceUpdateListener(InstanceUpdateListener l);
    public native InstanceUpdateListener instanceUpdateListener();

    public native Signal setGroup(long grp);
    public native long group();

    private native void mapperSignalReserveInstances(long sig, int num, long[] ids);
    public Signal reserveInstances(int num) {
        mapperSignalReserveInstances(_sig, num, null);
        return this;
    }
    public Signal reserveInstances(long[] ids) {
        mapperSignalReserveInstances(_sig, 0, ids);
        return this;
    }

    public Instance instance(long id) {
        return new Instance(this, id);
    }
    public Instance instance() {
        return new Instance(this);
    }
    public Instance instance(Object obj) {
        return new Instance(this, obj);
    }

    /* instance management */
    public native Instance oldestActiveInstance();
    public native Instance newestActiveInstance();

    public native int numActiveInstances();
    public native int numReservedInstances();

    private native void mapperSetInstanceStealingMode(int mode);
    public Signal setInstanceStealingMode(StealingMode mode) {
        mapperSetInstanceStealingMode(mode.value());
        return this;
    }
    private native int mapperInstanceStealingMode(long sig);
    public StealingMode instanceStealingMode() {
        return StealingMode.values()[mapperInstanceStealingMode(_sig)];
    }

    /* update signal or instance */
    private native Signal updateInstance(long id, int value, TimeTag tt);
    private native Signal updateInstance(long id, float value, TimeTag tt);
    private native Signal updateInstance(long id, double value, TimeTag tt);
    private native Signal updateInstance(long id, int[] value, TimeTag tt);
    private native Signal updateInstance(long id, float[] value, TimeTag tt);
    private native Signal updateInstance(long id, double[] value, TimeTag tt);

    public Signal update(int value) {
        return updateInstance(0, value, null);
    }
    public Signal update(float value) {
        return updateInstance(0, value, null);
    }
    public Signal update(double value) {
        return updateInstance(0, value, null);
    }
    public Signal update(int[] value) {
        return updateInstance(0, value, null);
    }
    public Signal update(float[] value) {
        return updateInstance(0, value, null);
    }
    public Signal update(double[] value) {
        return updateInstance(0, value, null);
    }

    public Signal update(int value, TimeTag tt) {
        return updateInstance(0, value, tt);
    }
    public Signal update(float value, TimeTag tt) {
        return updateInstance(0, value, tt);
    }
    public Signal update(double value, TimeTag tt) {
        return updateInstance(0, value, tt);
    }
    public Signal update(int[] value, TimeTag tt) {
        return updateInstance(0, value, tt);
    }
    public Signal update(float[] value, TimeTag tt) {
        return updateInstance(0, value, tt);
    }
    public Signal update(double[] value, TimeTag tt) {
        return updateInstance(0, value, tt);
    }

    /* signal or instance value */
    private native Value instanceValue(long id);

    public Value value() {
        return instanceValue(0);
    }

    public class Instance
    {
        private native long mapperInstance(boolean hasId, long id, Object obj);
        private Instance(Signal sig, long id) {
            _sigobj = sig;
            _sigptr = sig._sig;
            _id = mapperInstance(true, id, null);
        }
        private Instance(Signal sig, Object obj) {
            _sigobj = sig;
            _sigptr = sig._sig;
            _id = mapperInstance(false, 0, obj);
        }
        private Instance(Signal sig) {
            _sigobj = sig;
            _sigptr = sig._sig;
            _id = mapperInstance(false, 0, null);
        }

        /* release */
        public native void release(TimeTag tt);
        public void release() {
            release(null);
        }

        /* remove instances */
        private native void mapperFreeInstance(long sig, long id, TimeTag tt);
        public void free(TimeTag tt) {
            mapperFreeInstance(_sigptr, _id, tt);
        }
        public void free() {
            mapperFreeInstance(_sigptr, _id, null);
        }

        public Signal signal() { return _sigobj; }

        public native int isActive();
        public long id() { return _id; }

        /* update */
        public Instance update(int value, TimeTag tt) {
            updateInstance(_id, value, tt);
            return this;
        }
        public Instance update(float value, TimeTag tt) {
            updateInstance(_id, value, tt);
            return this;
        }
        public Instance update(double value, TimeTag tt) {
            updateInstance(_id, value, tt);
            return this;
        }
        public Instance update(int[] value, TimeTag tt) {
            updateInstance(_id, value, tt);
            return this;
        }
        public Instance update(float[] value, TimeTag tt) {
            updateInstance(_id, value, tt);
            return this;
        }
        public Instance update(double[] value, TimeTag tt) {
            updateInstance(_id, value, tt);
            return this;
        }

        public Instance update(int value) {
            updateInstance(_id, value, null);
            return this;
        }
        public Instance update(float value) {
            updateInstance(_id, value, null);
            return this;
        }
        public Instance update(double value) {
            updateInstance(_id, value, null);
            return this;
        }
        public Instance update(int[] value) {
            updateInstance(_id, value, null);
            return this;
        }
        public Instance update(float[] value) {
            updateInstance(_id, value, null);
            return this;
        }
        public Instance update(double[] value) {
            updateInstance(_id, value, null);
            return this;
        }

        /* value */
        public Value value() {
            return instanceValue(_id);
        }

        /* userObject */
        public native Object userReference();
        public native Instance setUserReference(Object obj);

        private Signal _sigobj;
        private long _sigptr;
        private long _id;
    }

    /* query remotes */
    public native int queryRemotes(TimeTag tt);
    public int queryRemotes() { return queryRemotes(null); };

    /* property */
    public native int numProperties();
    public native Value property(String property);
    public native Property property(int index);
    public native Signal setProperty(String name, Value value, boolean publish);
    public Signal setProperty(String name, Value value) {
        return setProperty(name, value, true);
    }
    public Signal setProperty(Property prop) {
        return setProperty(prop.name, prop.value, prop.publish);
    }
    public native Signal removeProperty(String name);
    public Signal removeProperty(Property prop) {
        return removeProperty(prop.name);
    }

    /* clear staged properties */
    public native Signal clearStagedProperties();

    /* push */
    public native Signal push();

    /* property: direction */
    private native int direction(long sig);
    public Direction direction() {
        return Direction.values()[direction(_sig)];
    }

    /* property: id */
    public native long id();

    /* property: is_local */
    public native boolean isLocal();

    /* property: length */
    public native int length();

    /* property: maximum */
    public native Value maximum();
    public native Signal setMaximum(Value p);

    /* property: minimum */
    public native Value minimum();
    public native Signal setMinimum(Value p);

    /* property: name */
    public native String name();

    /* property: rate */
    public native float rate();
    public native Signal setRate(float rate);

    /* property: type */
    public native char type();

    /* property: unit */
    public native String unit();

    /* retrieve associated maps */
    public native int numMaps(int direction);
    public int numMaps() {
        return numMaps(Direction.ANY.value());
    }
    public int numIncomingMaps() {
        return numMaps(Direction.INCOMING.value());
    }
    public int numOutgoingMaps() {
        return numMaps(Direction.OUTGOING.value());
    }

    public native mapper.map.Query maps(int direction);
    public mapper.map.Query maps() {
        return maps(Direction.ANY.value());
    }
    public mapper.map.Query incomingMaps() {
        return maps(Direction.INCOMING.value());
    }
    public mapper.map.Query outgoingMaps() {
        return maps(Direction.OUTGOING.value());
    }

    private long _sig;
    public boolean valid() {
        return _sig != 0;
    }

    static {
        System.loadLibrary(NativeLib.name);
    }
}
