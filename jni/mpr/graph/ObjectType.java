
package mpr.graph;

/*! Bit flags for coordinating graph metadata subscriptions. */
public enum ObjectType {
    NONE            (0x00),
    DEVICE          (0x01),
    INPUT_SIGNAL    (0x02),
    OUTPUT_SIGNAL   (0x04),
    SIGNAL          (0x06),
    INCOMING_MAP    (0x40),
    OUTGOING_MAP    (0x80),
    MAP             (0xC0),
    ALL             (0xFF);

    ObjectType(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
