
package mapper.db;

/*! The set of possible actions on a db entity. */
public enum Event
{
    ADDED(0), MODIFIED(1), REMOVED(2), EXPIRED(3);

    Event(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
