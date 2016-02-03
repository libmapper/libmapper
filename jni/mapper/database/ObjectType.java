
package mapper.database;

/*! Bit flags for coordinating database metadata subscriptions. */
public enum ObjectType {
    NONE            (0x00),
    DEVICES         (0x01),
    INPUT_SIGNALS   (0x02),
    OUTPUT_SIGNALS  (0x04),
    SIGNALS         (0x06),
    INCOMING_MAPS   (0x10),
    OUTGOING_MAPS   (0x20),
    MAPS            (0x30),
    ALL             (0xFF);

    ObjectType(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
