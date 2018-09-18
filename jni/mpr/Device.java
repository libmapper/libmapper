
package mpr;

//import mpr.NativeLib;
import mpr.signal.UpdateListener;
import mpr.signal.InstanceUpdateListener;
import mpr.Type;
import mpr.Time;

public class Device extends mpr.AbstractObject
{
    /* constructor */
    private native long mprDeviceNew(String name, Graph g);
    public Device(String name) {
        super(0);
        _obj = mprDeviceNew(name, null);
    }
    public Device(String name, Graph g) {
        super(0);
        _obj = mprDeviceNew(name, g);
    }
    public Device(long dev) {
        super(dev);
    }

    /* self */
    @Override
    public Device self() {
        return this;
    }

    /* poll */
    public native int poll(int timeout);
    public int poll() { return poll(0); }

    /* signals */
    public native Signal addSignal(Direction dir, int numInstances, String name,
                                   int length, Type type, String unit,
                                   Number minimum, Number maximum,
                                   mpr.signal.UpdateListener l);
    public Signal addSignal(Direction dir, String name, int length, Type type) {
        return addSignal(dir, 1, name, length, type, null, null, null, null);
    }
    public native Device removeSignal(Signal sig);

    /* signal groups */
    public native long addSignalGroup();
    public native Device removeSignalGroup(long group);

    /* property: ready */
    public native boolean ready();

    /* manage update queues */
    public native Time startQueue(Time t);
    public Time startQueue() {
        return startQueue(null);
    }
    public native Device sendQueue(Time t);

    /* retrieve associated signals */
    private native long signals(long dev, int dir);
    public mpr.List<mpr.Signal> signals(Direction dir)
        { return new mpr.List<mpr.Signal>(signals(_obj, dir.value())); }
    public mpr.List<mpr.Signal> signals()
        { return new mpr.List<mpr.Signal>(signals(_obj, Direction.ANY.value())); }
}
