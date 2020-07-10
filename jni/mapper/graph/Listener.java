
package mapper.graph;

public class Listener<T extends mapper.AbstractObject> {
    public void onEvent(mapper.Device device, mapper.graph.Event event) {};
    public void onEvent(mapper.Signal signal, mapper.graph.Event event) {};
    public void onEvent(mapper.Map map, mapper.graph.Event event) {};

}
