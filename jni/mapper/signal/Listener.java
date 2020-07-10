
package mapper.signal;

import mapper.Signal;

public class Listener {
    // singleton
    public void onEvent(Signal s, Event e, float f, mapper.Time t) {};
    public void onEvent(Signal s, Event e, int i, mapper.Time t) {};
    public void onEvent(Signal s, Event e, double d, mapper.Time t) {};
    public void onEvent(Signal s, Event e, float[] f, mapper.Time t) {};
    public void onEvent(Signal s, Event e, int[] i, mapper.Time t) {};
    public void onEvent(Signal s, Event e, double[] d, mapper.Time t) {};

    // instanced
    public void onEvent(Signal.Instance s, Event e, float f, mapper.Time t) {};
    public void onEvent(Signal.Instance s, Event e, int i, mapper.Time t) {};
    public void onEvent(Signal.Instance s, Event e, double d, mapper.Time t) {};
    public void onEvent(Signal.Instance s, Event e, float[] f, mapper.Time t) {};
    public void onEvent(Signal.Instance s, Event e, int[] i, mapper.Time t) {};
    public void onEvent(Signal.Instance s, Event e, double[] d, mapper.Time t) {};
}
