
package mapper;

/*! Describes the directionality of map endpoints. */
public enum Direction {
    UNDEFINED   (0),
    IN          (1),
    OUT         (2),
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
