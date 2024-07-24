
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
    HAS_VALUE   (0x0040),
    NEW_VALUE   (0x0080),
    UPDATE_LOC  (0x0100),
    UPDATE_REM  (0x0200),
    REL_UPSTRM  (0x0400),
    REL_DNSTRM  (0x0800),
    OVERFLOW    (0x1000),
    ANY         (0x1FFF);

    Status(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
