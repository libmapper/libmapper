
package mapper.signal;

/*! Describes possible statuses for a libmapper signal instance. */
public enum InstanceStatus {
    UNDEFINED   (0x00),
    RESERVED    (0x04),
    ACTIVE      (0x10),
    HAS_VALUE   (0x08),
    NEW         (0x02),
    UPDATE_LOC  (0x40),
    UPDATE_REM  (0x80),
    REL_UPSTRM  (0x20),
    REL_DNSTRM  (0x01),
    OVERFLOW    (0x10),
    ANY         (0xFF);

    InstanceStatus(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
