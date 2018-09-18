
package mpr.graph;

public class Listener<T extends mpr.AbstractObject> {
    public void onEvent(T record, mpr.graph.Event event) {};
}
