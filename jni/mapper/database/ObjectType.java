
package mapper.database;

/*! Bit flags for coordinating database metadata subscriptions. */
public enum ObjectType {
    NONE            (0x00),
    DEVICES         (0x01),
    INPUT_SIGNALS   (0x02),
    OUTPUT_SIGNALS  (0x04),
    SIGNALS         (0x06),
    INCOMING_LINKS  (0x10),
    OUTGOING_LINKS  (0x20),
    LINKS           (0x30),
    INCOMING_MAPS   (0x40),
    OUTGOING_MAPS   (0x80),
    MAPS            (0xC0),
    ALL             (0xFF);

    ObjectType(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
