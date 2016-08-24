
package mapper.map;

/*! Describes what happens when the range boundaries are exceeded. */
public enum BoundaryAction {
    UNDEFINED   (0),
    NONE        (1),
    MUTE        (2),
    CLAMP       (3),
    FOLD        (4),
    WRAP        (5);

    BoundaryAction(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}