
package mapper.database;

/*! Bit flags for coordinating database metadata subscriptions. */
public enum SubscriptionType {
    NONE            (0x00),
    DEVICES         (0x01),
    DEVICE_INPUTS   (0x02),
    DEVICE_OUTPUTS  (0x04),
    DEVICE_SIGNALS  (0x06),
    DEVICE_MAPS_IN  (0x10),
    DEVICE_MAPS_OUT (0x20),
    DEVICE_MAPS     (0x30),
    ALL             (0xFF);

    SubscriptionType(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
