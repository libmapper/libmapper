
package mapper;

/*! Describes the directionality of map slots. */
public enum Type {
    INT         ('i'),
    LONG        ('h'),
    FLOAT       ('f'),
    DOUBLE      ('d'),
    STRING      ('s'),
    BOOL        ('b'),
    TIMETAG     ('t'),
    CHAR        ('c'),
    DEVICE      ('D'),
    NULL        ('N');

    Type(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
