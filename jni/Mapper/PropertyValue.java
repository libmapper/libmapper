
package Mapper;

/* Contains a value representable by supported types, which correspond
 * with OSC types. */
public class PropertyValue
{
    public PropertyValue()                     { type = 0;           }
    public PropertyValue(int i)                { setValue('i', i);   }
    public PropertyValue(float f)              { setValue('f', f);   }
    public PropertyValue(double d)             { setValue('d', d);   }
    public PropertyValue(String s)             { setValue('s', s);   }
    public PropertyValue(char _type, int i)    { setValue(_type, i); }
    public PropertyValue(char _type, float f)  { setValue(_type, f); }
    public PropertyValue(char _type, double d) { setValue(_type, d); }
    public PropertyValue(char _type, String s) { setValue(_type, s); }

    // TODO handle entire set of OSC types
    public class PropertyException extends RuntimeException {
        public PropertyException() {
            super("Property does not contain requested type.");
        }
        public PropertyException(String msg) {
            super(msg);
        }
    }

    public Object value() {
        switch (type) {
        case 'i': return new Integer(_i);
        case 'f': return new Float(_f);
        case 'd': return new Double(_d);
        case 's': return _s;
        case 'S': return _s;
        }
        return null;
    }

    public int intValue() {
        if (type == 'i')
            return _i;
        throw new PropertyException();
    }

    public float floatValue() {
        if (type == 'f')
            return _f;
        else if (type == 'i')
            return _i;
        throw new PropertyException();
    }

    public double doubleValue() {
        if (type == 'd')
            return _d;
        else if (type == 'f')
            return _f;
        else if (type == 'i')
            return _i;
        throw new PropertyException();
    }

    public String stringValue() {
        if (type == 's' || type == 'S')
            return _s;
        throw new PropertyException();
    }

    public void setValue(char _type, int i) {
        switch (_type) {
        case 'i':
            _i = i; break;
        case 'f':
            _f = i; break;
        case 'd':
            _d = i; break;
        case 's':
        case 'S':
            _s = String.valueOf(i);
            break;
        default:
            throw new PropertyException("Cannot cast int to requested type.");
        }
        type = _type;
    }

    public void setValue(char _type, float f) {
        switch (_type) {
        case 'i':
            _i = (int)f; break;
        case 'f':
            _f = f; break;
        case 'd':
            _d = f; break;
        case 's':
        case 'S':
            _s = String.valueOf(f);
            break;
        default:
            throw new PropertyException("Cannot cast float to requested type.");
        }
        type = _type;
    }

    public void setValue(char _type, double d) {
        switch (_type) {
        case 'i':
            _i = (int)d; break;
        case 'f':
            _f = (float)d; break;
        case 'd':
            _d = d; break;
        case 's':
        case 'S':
            _s = String.valueOf(d);
            break;
        default:
            throw new PropertyException("Cannot cast double to requested type.");
        }
        type = _type;
    }

    public void setValue(char _type, String s) {
        switch (_type) {
        case 'i':
            _i = Integer.parseInt(s); break;
        case 'f':
            _f = Float.parseFloat(s); break;
        case 'd':
            _d = Double.parseDouble(s); break;
        case 's':
        case 'S':
            _s = s; break;
        default:
            throw new PropertyException("Cannot cast String to requested type.");
        }
        type = _type;
    }

    public String toString() {
        String s;
        switch (type) {
        case 'i': s = String.valueOf(_i); break;
        case 'f': s = String.valueOf(_f); break;
        case 'd': s = String.valueOf(_d); break;
        case 's':s = '\''+_s+'\''; break;
        case 'S': s = '"'+_s+'"'; break;
        default: s = null;
        }
        return "<type="+type+", value="+s+">";
    }

    public char type;

    private int _i;
    private float _f;
    private double _d;
    private String _s;
}
