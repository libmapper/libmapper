
package mpr;

//import mpr.NativeLib;
import mpr.signal.Listener;
import mpr.Type;
import mpr.Time;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

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
    private native Signal add_signal(long dev, int dir, String name, int length,
                                     int type, String unit,
                                     java.lang.Object minimum,
                                     java.lang.Object maximum,
                                     Integer numInstances,
                                     mpr.signal.Listener l, String methodSig);
    public Signal addSignal(Direction dir, String name, int length, Type type,
                            String unit, java.lang.Object minimum,
                            java.lang.Object maximum, Integer numInstances,
                            mpr.signal.Listener l) {
        if (l == null)
            return add_signal(_obj, dir.value(), name, length, type.value(),
                              unit, minimum, maximum, numInstances, null, null);

        // try to check type of listener
        String[] instanceName = l.toString().split("@", 0);
        for (Method method : l.getClass().getMethods()) {
            if ((method.getModifiers() & Modifier.STATIC) != 0)
                continue;
            String methodName = method.toString();
            if (methodName.startsWith("public void "+instanceName[0]+".")) {
                return add_signal(_obj, dir.value(), name, length, type.value(),
                                  unit, minimum, maximum, numInstances, l,
                                  methodName);
            }
        }
        return null;
    }
    public Signal addSignal(Direction dir, String name, int length, Type type) {
        return add_signal(_obj, dir.value(), name, length, type.value(),
                          null, null, null, null, null, null);
    }
    public native Device removeSignal(Signal sig);

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
