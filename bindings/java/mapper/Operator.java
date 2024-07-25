
package mapper;

/*! Possible operations for composing graph queries. */
public enum Operator {
    DOES_NOT_EXIST          (0x01),
    EQUAL                   (0x02),
    EXISTS                  (0x03),
    GREATER_THAN            (0x04),
    GREATER_THAN_OR_EQUAL   (0x05),
    LESS_THAN               (0x06),
    LESS_THAN_OR_EQUAL      (0x07),
    NOT_EQUAL               (0x08),
    BIT_AND                 (0x09),
    BIT_OR                  (0x0A),
    ALL                     (0x10),
    ANY                     (0x20),
    NONE                    (0x40);

    Operator(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
