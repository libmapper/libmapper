
package mpr.graph;

public class Listener<T extends mpr.AbstractObject> {
    public void onEvent(mpr.Device device, mpr.graph.Event event) {};
    public void onEvent(mpr.Signal signal, mpr.graph.Event event) {};
    public void onEvent(mpr.Map map, mpr.graph.Event event) {};

}
