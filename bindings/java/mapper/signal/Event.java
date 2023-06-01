
package mapper.signal;

/*! The set of possible actions on an instance, used to register callbacks
 *  to inform them of what is happening. */
public enum Event {
    NEW_INSTANCE        (0x01),
    UPSTREAM_RELEASE    (0x02),
    DOWNSTREAM_RELEASE  (0x04),
    OVERFLOW            (0x08),
    UPDATE              (0x10),
    ALL                 (0x1F);

    Event(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
