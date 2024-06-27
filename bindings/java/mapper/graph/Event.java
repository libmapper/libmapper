
package mapper.graph;

/*! The set of possible events for a graph entity. */
public enum Event
{
    UNDEDFINED  (0x00),
    NEW         (0x01),
    MODIFIED    (0x02),
    REMOVED     (0x04),
    EXPIRED     (0x08);

    Event(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
