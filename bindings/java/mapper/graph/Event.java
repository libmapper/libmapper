
package mapper.graph;

/*! The set of possible events for a graph entity. */
public enum Event
{
    NEW(0), MODIFIED(1), REMOVED(2), EXPIRED(3);

    Event(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
