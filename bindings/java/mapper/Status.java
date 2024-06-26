
package mapper;

/*! Describes possible statuses for a libmapper object. */
public enum Status {
    UNDEFINED   (0x00),
    EXPIRED     (0x01),
    NEW         (0x02),
    MODIFIED    (0x04),
    STAGED      (0x08),
    ACTIVE      (0x10),
    REMOVED     (0x20),
    ANY         (0xFF);

    Status(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
