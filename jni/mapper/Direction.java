
package mapper;

/*! Describes the directionality of map slots. */
public enum Direction {
    UNDEFINED   (0),
    INCOMING    (1),
    OUTGOING    (2),
    ANY         (3),
    BOTH        (4);

    Direction(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
