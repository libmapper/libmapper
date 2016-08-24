
package mapper.signal;

/*! Describes the voice-stealing mode for instances. */
public enum StealingMode {
    NONE    (0),
    OLDEST  (1),
    NEWEST  (2);

    StealingMode(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}