
package mapper;

/* Symbolic identifiers for various properties. */
public enum Property
{
    UNKNOWN             (0x0000),
    CALIBRATING         (0x0100),
    DEVICE              (0x0200),
    DIRECTION           (0x0300),
    EXPRESSION          (0x0400),
    HOST                (0x0500),
    ID                  (0x0600),
    INSTANCE            (0x0700),
    IS_LOCAL            (0x0800),
    LENGTH              (0x0900),
    LIB_VERSION         (0x0A00),
    MAX                 (0x0B00),
    MIN                 (0x0C00),
    MUTED               (0x0D00),
    NAME                (0x0E00),
    NUM_INPUTS          (0x0F00),
    NUM_INSTANCES       (0x1000),
    NUM_MAPS            (0x1100),
    NUM_MAPS_IN         (0x1200),
    NUM_MAPS_OUT        (0x1300),
    NUM_OUTPUTS         (0x1400),
    ORDINAL             (0x1500),
    PORT                (0x1600),
    PROCESS_LOCATION    (0x1700),
    PROTOCOL            (0x1800),
    RATE                (0x1900),
    SCOPE               (0x1A00),
    SIGNAL              (0x1B00),
    SLOT                (0x1C00),
    STATUS              (0x1D00),
    SYNCED              (0x1E00),
    TYPE                (0x1F00),
    UNIT                (0x2000),
    USE_INSTANCES       (0x2100),
    USER_DATA           (0x2200),
    VERSION             (0x2300),
    EXTRA               (0x2400);

    Property(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
