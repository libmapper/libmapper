
package mapper;

/* Symbolic identifiers for various properties. */
public enum Property
{
    UNKNOWN             (0x0000),
    BUNDLE              (0x0100),
    DATA                (0x0200),
    DEVICE              (0x0300),
    DIRECTION           (0x0400),
    EPHEMERAL           (0x0500),
    EXPRESSION          (0x0600),
    HOST                (0x0700),
    ID                  (0x0800),
    IS_LOCAL            (0x0900),
    JITTER              (0x0A00),
    LENGTH              (0x0B00),
    LIB_VERSION         (0x0C00),
    LINKED              (0x0D00),
    MAX                 (0x0E00),
    MIN                 (0x0F00),
    MUTED               (0x1000),
    NAME                (0x1100),
    NUM_INST            (0x1200),
    NUM_MAPS            (0x1300),
    NUM_MAPS_IN         (0x1400),
    NUM_MAPS_OUT        (0x1500),
    NUM_SIGS_IN         (0x1600),
    NUM_SIGS_OUT        (0x1700),
    ORDINAL             (0x1800),
    PERIOD              (0x1900),
    PORT                (0x1A00),
    PROCESS_LOC         (0x1B00),
    PROTOCOL            (0x1C00),
    RATE                (0x1D00),
    SCOPE               (0x1E00),
    SIGNAL              (0x1F00),
    /* SLOT DELIBERATELY OMITTED */
    STATUS              (0x2100),
    STEAL_MODE          (0x2200),
    SYNCED              (0x2300),
    TYPE                (0x2400),
    UNIT                (0x2500),
    USE_INST            (0x2600),
    VERSION             (0x2700),
    EXTRA               (0x2800);

    Property(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
