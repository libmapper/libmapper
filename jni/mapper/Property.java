
package mapper;

/* Symbolic identifiers for various properties. */
public enum Property
{
    UNKNOWN             (0x0000),
    DATA                (0x0100),
    DEVICE              (0x0200),
    DIRECTION           (0x0300),
    EXPRESSION          (0x0400),
    HOST                (0x0500),
    ID                  (0x0600),
    INSTANCE            (0x0700),
    IS_LOCAL            (0x0800),
    JITTER              (0x0900),
    LENGTH              (0x0A00),
    LIB_VERSION         (0x0B00),
    LINKED              (0x0C00),
    MAX                 (0x0D00),
    MIN                 (0x0E00),
    MUTED               (0x0F00),
    NAME                (0x1000),
    NUM_INST            (0x1100),
    NUM_MAPS            (0x1200),
    NUM_MAPS_IN         (0x1300),
    NUM_MAPS_OUT        (0x1400),
    NUM_SIGS_IN         (0x1500),
    NUM_SIGS_OUT        (0x1600),
    ORDINAL             (0x1700),
    PERIOD              (0x1800),
    PORT                (0x1900),
    PROCESS_LOC         (0x1A00),
    PROTOCOL            (0x1B00),
    RATE                (0x1C00),
    SCOPE               (0x1D00),
    SIGNAL              (0x1E00),
    SLOT                (0x1F00),
    STATUS              (0x2000),
    STEAL_MODE          (0x2100),
    SYNCED              (0x2200),
    TYPE                (0x2300),
    UNIT                (0x2400),
    USE_INST            (0x2500),
    VERSION             (0x2600),
    EXTRA               (0x2700);

    Property(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
