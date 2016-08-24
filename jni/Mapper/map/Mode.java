
package mapper.map;

/*! Describes the mapping mode. */
public enum Mode {
    UNDEFINED   (0),
    RAW         (1),
    LINEAR      (2),
    EXPRESSION  (3);

    Mode(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}