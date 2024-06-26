
package mapper.graph;

/*! The set of possible events for a graph entity. */
public enum Event
{
    UNDEDFINED  (0x00),
    EXPIRED     (0x01),
    NEW         (0x02),
    MODIFIED    (0x04),
    REMOVED     (0x20);

    Event(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
