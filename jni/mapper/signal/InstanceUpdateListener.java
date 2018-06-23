
package mapper.signal;

import mapper.Signal;

public class InstanceUpdateListener {
    public void onUpdate(Signal.Instance s, float[] f, mapper.Time t) {};
    public void onUpdate(Signal.Instance s, int[] i, mapper.Time t) {};
    public void onUpdate(Signal.Instance s, double[] d, mapper.Time t) {};
}
