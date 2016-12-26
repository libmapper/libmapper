
package mapper.signal;

import mapper.Signal;

public class InstanceUpdateListener {
    public void onUpdate(Signal.Instance sig, float[] v, mapper.TimeTag tt) {};
    public void onUpdate(Signal.Instance sig, int[] v, mapper.TimeTag tt) {};
    public void onUpdate(Signal.Instance sig, double[] v, mapper.TimeTag tt) {};
}
