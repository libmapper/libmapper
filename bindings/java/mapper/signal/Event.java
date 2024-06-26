
package mapper.signal;

/*! The set of possible actions on an instance, used to register callbacks
 *  to inform them of what is happening. */
public enum Event {
    NEW_INSTANCE        (0x02),
    UPSTREAM_RELEASE    (0x20),
    DOWNSTREAM_RELEASE  (0x01),
    OVERFLOW            (0x10),
    UPDATE              (0x80),
    ALL                 (0xFF);

    Event(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
