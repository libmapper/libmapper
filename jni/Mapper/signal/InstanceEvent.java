
package mapper.signal;

/*! The set of possible actions on an instance, used to register callbacks
 *  to inform them of what is happening. */
public enum InstanceEvent {
    NEW_INSTANCE        (0x01),
    UPSTREAM_RELEASE    (0x02),
    DOWNSTREAM_RELEASE  (0x04),
    OVERFLOW            (0x08),
    ALL                 (0xFF);

    InstanceEvent(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    public static InstanceEvent getInstance(int value) {
        switch (value) {
            case 0x01:
                return NEW_INSTANCE;
            case 0x02:
                return UPSTREAM_RELEASE;
            case 0x04:
                return DOWNSTREAM_RELEASE;
            case 0x08:
                return OVERFLOW;
            default:
                return ALL;
        }
    }

    private int _value;
}