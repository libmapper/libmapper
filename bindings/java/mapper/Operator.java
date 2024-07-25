
package mapper;

/*! Possible operations for composing graph queries. */
public enum Operator {
    NEX (0x01),
    EQ  (0x02),
    EX  (0x03),
    GT  (0x04),
    GTE (0x05),
    LT  (0x06),
    LTE (0x07),
    NEQ (0x08);
    AND (0x09);
    OR  (0x0A);
    ALL (0x10);
    ANY (0x20);
    NONE(0x40);

    Operator(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
