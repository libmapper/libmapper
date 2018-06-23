
package mapper;

//import mapper.NativeLib;
import mapper.signal.UpdateListener;
import mapper.signal.InstanceUpdateListener;
import mapper.Type;
import mapper.Time;

public class Device extends mapper.AbstractObject
{
    /* constructor */
    private native long mapperDeviceNew(String name, Graph g);
    public Device(String name) {
        super(0);
        _obj = mapperDeviceNew(name, null);
    }
    public Device(String name, Graph g) {
        super(0);
        _obj = mapperDeviceNew(name, g);
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
                                   mapper.signal.UpdateListener l);
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
    public mapper.List<mapper.Signal> signals(Direction dir)
        { return new mapper.List<mapper.Signal>(signals(_obj, dir.value())); }
    public mapper.List<mapper.Signal> signals()
        { return new mapper.List<mapper.Signal>(signals(_obj, Direction.ANY.value())); }

    /* retrieve associated maps */
    private native long maps(long dev, int dir);
    public mapper.List<mapper.Map> maps(Direction dir)
        { return new mapper.List<mapper.Map>(maps(_obj, dir.value())); }
    public mapper.List<mapper.Map> maps()
        { return maps(Direction.ANY); }
}
