
package mapper.map;

/*! The set of possible network protocols. */
public enum Protocol {
    UNDEFINED   (0),
    UDP         (1),
    TCP         (2);

    Protocol(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
