
package mapper.signal;

/*! The set of possible actions on an instance, used to register callbacks
 *  to inform them of what is happening. */
public enum Event {
    NEW_INSTANCE        (0x0001),
    UPDATE              (0x0200),
    UPSTREAM_RELEASE    (0x0400),
    DOWNSTREAM_RELEASE  (0x0800),
    OVERFLOW            (0x1000),
    ALL                 (0xFFFF);

    Event(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
