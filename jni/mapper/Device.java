
package mapper;

//import mapper.NativeLib;
import mapper.signal.Listener;
import mapper.Type;
import mapper.Time;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

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

    /* free */
    private native void mapperDeviceFree(long obj);
    public void free() {
        if (_obj != 0)
            mapperDeviceFree(_obj);
        _obj = 0;
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
                                     mapper.signal.Listener l, String methodSig);
    public Signal addSignal(Direction dir, String name, int length, Type type,
                            String unit, java.lang.Object minimum,
                            java.lang.Object maximum, Integer numInstances,
                            mapper.signal.Listener l) {
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

    public native Time getTime();
    public native Device setTime(Time t);
//    public native Device updateDone();

    /* retrieve associated signals */
    private native long signals(long dev, int dir);
    public mapper.List<mapper.Signal> signals(Direction dir)
        { return new mapper.List<mapper.Signal>(signals(_obj, dir.value())); }
    public mapper.List<mapper.Signal> signals()
        { return new mapper.List<mapper.Signal>(signals(_obj, Direction.ANY.value())); }
}
