
package mapper.map;

/*! The set of possible locations for map stream processing to be performed. */
public enum Location {
    UNDEFINED   (0),
    SOURCE      (1),
    DESTINATION (2),
    ANY         (3);

    Location(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
