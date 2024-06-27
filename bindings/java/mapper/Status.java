
package mapper;

/*! Describes possible statuses for a libmapper object. */
public enum Status {
    UNDEFINED   (0x0000),
    NEW         (0x0001),
    MODIFIED    (0x0002),
    REMOVED     (0x0004),
    EXPIRED     (0x0008),
    STAGED      (0x0010),
    ACTIVE      (0x0020),
    ANY         (0xFF);

    Status(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
