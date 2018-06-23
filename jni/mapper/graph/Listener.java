
package mapper.graph;

public class Listener<T extends mapper.AbstractObject> {
    public void onEvent(T record, mapper.graph.Event event) {};
}
