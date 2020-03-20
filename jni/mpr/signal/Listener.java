
package mpr.signal;

import mpr.Signal;

public class Listener {
    // singleton
    public void onEvent(Signal s, Event e, float f, mpr.Time t) {};
    public void onEvent(Signal s, Event e, int i, mpr.Time t) {};
    public void onEvent(Signal s, Event e, double d, mpr.Time t) {};
    public void onEvent(Signal s, Event e, float[] f, mpr.Time t) {};
    public void onEvent(Signal s, Event e, int[] i, mpr.Time t) {};
    public void onEvent(Signal s, Event e, double[] d, mpr.Time t) {};

    // instanced
    public void onEvent(Signal.Instance s, Event e, float f, mpr.Time t) {};
    public void onEvent(Signal.Instance s, Event e, int i, mpr.Time t) {};
    public void onEvent(Signal.Instance s, Event e, double d, mpr.Time t) {};
    public void onEvent(Signal.Instance s, Event e, float[] f, mpr.Time t) {};
    public void onEvent(Signal.Instance s, Event e, int[] i, mpr.Time t) {};
    public void onEvent(Signal.Instance s, Event e, double[] d, mpr.Time t) {};
}
