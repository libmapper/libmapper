
package mapper;

/*! Describes possible statuses for a libmapper object. */
public enum Status {
    UNDEFINED   (0x00),
    EXPIRED     (0x01),
    STAGED      (0x02),
    READY       (0x3E),
    ACTIVE      (0X7E),
    RESERVED    (0X80),
    ANY         (0xFF);

    Status(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
