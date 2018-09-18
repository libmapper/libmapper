
package mpr.signal;

import mpr.Signal;

public class InstanceUpdateListener {
    public void onUpdate(Signal.Instance s, float[] f, mpr.Time t) {};
    public void onUpdate(Signal.Instance s, int[] i, mpr.Time t) {};
    public void onUpdate(Signal.Instance s, double[] d, mpr.Time t) {};
}
