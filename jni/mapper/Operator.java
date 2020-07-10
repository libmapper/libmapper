
package mapper;

/*! Possible operations for composing graph queries. */
public enum Operator {
    NEX (0),
    EQ  (1),
    EX  (2),
    GT  (3),
    GTE (4),
    LT  (5),
    LTE (6),
    NEQ (7);

    Operator(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
