
package mapper;

/*! Describes possible datatypes. */
public enum Type {
    DEVICE      (0x00),
    SIGNAL_IN   (0x02),
    SIGNAL_OUT  (0x04),
    SIGNAL      (0x06),
    MAP_IN      (0x08),
    MAP_OUT     (0x10),
    MAP         (0x18),
    OBJECT      (0x1F),
    LIST        (0x40),
    BOOL        ('b'),
    TYPE        ('c'),
    DOUBLE      ('d'),
    FLOAT       ('f'),
    INT64       ('h'),
    INT32       ('i'),
    STRING      ('s'),
    TIME        ('t'),
    POINTER     ('v'),
    NULL        ('N');

    Type(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
