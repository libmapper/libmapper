
package mapper.database;

/*! The set of possible actions on a database entity. */
public enum Event
{
    ADDED(0), MODIFIED(1), REMOVED(2), EXPIRED(3);

    Event(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    public static Event getInstance(int value) {
        switch (value) {
            case 0:
                return ADDED;
            case 1:
                return MODIFIED;
            case 2:
                return REMOVED;
            default:
                return EXPIRED;
        }
    }

    private int _value;
}
