
package mapper;
import mapper.TimeTag;
import java.util.Arrays;

/* Contains a value representable by supported types, which correspond
 * with OSC types. */
public class Value
{
    public Value()                       { length=0; type = 0; }
    public Value(boolean b)              { set('b', b);   }
    public Value(int i)                  { set('i', i);   }
    public Value(float f)                { set('f', f);   }
    public Value(double d)               { set('d', d);   }
    public Value(String s)               { set('s', s);   }
    public Value(boolean[] b)            { set('b', b);   }
    public Value(int[] i)                { set('i', i);   }
    public Value(float[] f)              { set('f', f);   }
    public Value(double[] d)             { set('d', d);   }
    public Value(String[] s)             { set('s', s);   }
    public Value(char _type, int i)      { set(_type, i); }
    public Value(char _type, float f)    { set(_type, f); }
    public Value(char _type, double d)   { set(_type, d); }
    public Value(char _type, String s)   { set(_type, s); }
    public Value(char _type, int[] i)    { set(_type, i); }
    public Value(char _type, float[] f)  { set(_type, f); }
    public Value(char _type, double[] d) { set(_type, d); }
    public Value(char _type, String[] s) { set(_type, s); }

    // TODO handle entire set of OSC types
    public class Exception extends RuntimeException {
        public Exception() {
            super("Value does not contain requested type.");
        }
        public Exception(String msg) {
            super(msg);
        }
    }

    public boolean isEmpty() {
        return length == 0;
    }

    public Object value() {
        switch (type) {
            case 'b': {
                if (length == 1)
                    return new Boolean(_b[0]);
                else if (length > 1) {
                    boolean[] b = _b.clone();
                    return b;
                }
            }
            case 'i': {
                if (length == 1)
                    return new Integer(_i[0]);
                else if (length > 1) {
                    int[] v = _i.clone();
                    return v;
                }
            }
            case 'f': {
                if (length == 1)
                    return new Float(_f[0]);
                else if (length > 1) {
                    float[] v = _f.clone();
                    return v;
                }
            }
            case 'd': {
                if (length == 1)
                    return new Double(_d[0]);
                else if (length > 1) {
                    double[] v = _d.clone();
                    return v;
                }
            }
            case 's': {
                if (length == 1)
                    return _s[0];
                else if (length > 1) {
                    String[] v = _s.clone();
                    return v;
                }
            }
        }
        return null;
    }

    public boolean booleanValue() {
        if (type == 'b')
            return _b[0];
        throw new Exception();
    }

    public int intValue() {
        if (type == 'i')
            return _i[0];
        throw new Exception();
    }

    public float floatValue() {
        if (type == 'f')
            return _f[0];
        else if (type == 'i')
            return _i[0];
        throw new Exception();
    }

    public double doubleValue() {
        if (type == 'd')
            return _d[0];
        else if (type == 'f')
            return _f[0];
        else if (type == 'i')
            return _i[0];
        throw new Exception();
    }

    public String stringValue() {
        if (type == 's')
            return _s[0];
        throw new Exception();
    }

    public void set(char _type, boolean b) {
        switch (_type) {
            case 'b':
                _b = new boolean[1];
                _b[0] = b;
                break;
            case 'i':
                _i = new int[1];
                _i[0] = b ? 1 : 0;
                break;
            case 'f':
                _f = new float[1];
                _f[0] = b ? 1 : 0;
                break;
            case 'd':
                _d = new double[1];
                _d[0] = b ? 1 : 0;
                break;
            case 's':
                _s = new String[1];
                _s[0] = String.valueOf(b);
                break;
            default:
                throw new Exception("Cannot cast boolean to requested type.");
        }
        type = _type;
        length = 1;
    }

    public void set(char _type, int i) {
        switch (_type) {
            case 'b':
                _b = new boolean[1];
                _b[0] = (i != 0);
                break;
            case 'i':
                _i = new int[1];
                _i[0] = i;
                break;
            case 'f':
                _f = new float[1];
                _f[0] = i;
                break;
            case 'd':
                _d = new double[1];
                _d[0] = i;
                break;
            case 's':
                _s = new String[1];
                _s[0] = String.valueOf(i);
                break;
            default:
                throw new Exception("Cannot cast int to requested type.");
        }
        type = _type;
        length = 1;
    }

    public void set(char _type, float f) {
        switch (_type) {
            case 'b':
                _b = new boolean[1];
                _b[0] = (f != 0.f);
                break;
            case 'i':
                _i = new int[1];
                _i[1] = (int)f;
                break;
            case 'f':
                _f = new float[1];
                _f[0] = f;
                break;
            case 'd':
                _d = new double[1];
                _d[0] = f;
                break;
            case 's':
                _s = new String[1];
                _s[0] = String.valueOf(f);
                break;
            default:
                throw new Exception("Cannot cast float to requested type.");
        }
        type = _type;
        length = 1;
    }

    public void set(char _type, double d) {
        switch (_type) {
            case 'b':
                _b = new boolean[1];
                _b[0] = (d != 0.0);
                break;
            case 'i':
                _i = new int[1];
                _i[0] = (int)d;
                break;
            case 'f':
                _f = new float[1];
                _f[0] = (float)d;
                break;
            case 'd':
                _d = new double[1];
                _d[0] = d;
                break;
            case 's':
                _s = new String[1];
                _s[0] = String.valueOf(d);
                break;
            default:
                throw new Exception("Cannot cast double to requested type.");
        }
        type = _type;
        length = 1;
    }

    public void set(char _type, String s) {
        switch (_type) {
            case 'b':
                _b = new boolean[1];
                _b[0] = Boolean.parseBoolean(s);
                break;
            case 'i':
                _i = new int[1];
                _i[0] = Integer.parseInt(s);
                break;
            case 'f':
                _f = new float[1];
                _f[0] = Float.parseFloat(s);
                break;
            case 'd':
                _d = new double[1];
                _d[0] = Double.parseDouble(s);
                break;
            case 's':
                _s = new String[1];
                _s[0] = s;
                break;
            default:
                throw new Exception("Cannot cast String to requested type.");
        }
        type = _type;
        length = 1;
    }

    public void set(char _type, boolean[] b) {
        switch (_type) {
            case 'b':
                _b = b.clone();
                break;
            case 'i':
                _i = new int[b.length];
                for (int n = 0; n < b.length; n++)
                    _i[n] = b[n] ? 1 : 0;
                break;
            case 'f':
                _f = new float[b.length];
                for (int n = 0; n < b.length; n++)
                    _f[n] = b[n] ? 1.f : 0.f;
                break;
            case 'd':
                _d = new double[b.length];
                for (int n = 0; n < b.length; n++)
                    _d[n] = b[n] ? 1.0 : 0.0;
                break;
            case 's':
                _s = new String[b.length];
                for (int n = 0; n < b.length; n++)
                    _s[n] = String.valueOf(b[n]);
                break;
            default:
                throw new Exception("Cannot cast int to requested type.");
        }
        type = _type;
        length = b.length;
    }

    public void set(char _type, int[] i) {
        switch (_type) {
            case 'b':
                _b = new boolean[i.length];
                for (int n = 0; n < i.length; n++)
                    _b[0] = (i[n] != 0);
                break;
            case 'i':
                _i = i.clone();
                break;
            case 'f':
                _f = new float[i.length];
                for (int n = 0; n < i.length; n++)
                    _f[n] = i[n];
                break;
            case 'd':
                _d = new double[i.length];
                for (int n = 0; n < i.length; n++)
                    _d[n] = i[n];
                break;
            case 's':
                _s = new String[i.length];
                for (int n = 0; n < i.length; n++)
                    _s[n] = String.valueOf(i[n]);
                break;
            default:
                throw new Exception("Cannot cast int to requested type.");
        }
        type = _type;
        length = i.length;
    }

    public void set(char _type, float[] f) {
        switch (_type) {
            case 'b':
                _b = new boolean[f.length];
                for (int n = 0; n < f.length; n++)
                    _b[0] = (f[n] != 0.f);
                break;
            case 'i':
                _i = new int[f.length];
                for (int n = 0; n < f.length; n++)
                    _i[n] = (int)f[n];
                break;
            case 'f':
                _f = f.clone();
                break;
            case 'd':
                _d = new double[f.length];
                for (int n = 0; n < f.length; n++)
                    _d[n] = f[n];
                break;
            case 's':
                _s = new String[f.length];
                for (int n = 0; n < f.length; n++)
                    _s[n] = String.valueOf(f[n]);
                break;
            default:
                throw new Exception("Cannot cast float to requested type.");
        }
        type = _type;
        length = f.length;
    }

    public void set(char _type, double[] d) {
        switch (_type) {
            case 'b':
                _b = new boolean[d.length];
                for (int n = 0; n < d.length; n++)
                    _b[0] = (d[n] != 0.0);
                break;
            case 'i':
                _i = new int[d.length];
                for (int n = 0; n < d.length; n++)
                    _i[n] = (int)d[n];
                break;
            case 'f':
                _f = new float[d.length];
                for (int n = 0; n < d.length; n++)
                    _f[n] = (float)d[n];
                break;
            case 'd':
                _d = d.clone();
                break;
            case 's':
                _s = new String[d.length];
                for (int n = 0; n < d.length; n++)
                    _s[n] = String.valueOf(d[n]);
                break;
            default:
                throw new Exception("Cannot cast double to requested type.");
        }
        type = _type;
        length = d.length;
    }

    public void set(char _type, String[] s) {
        switch (_type) {
            case 'b':
                _b = new boolean[s.length];
                for (int n = 0; n < s.length; n++)
                    _b[0] = Boolean.parseBoolean(s[n]);
                break;
            case 'i':
                _i = new int[s.length];
                for (int n = 0; n < s.length; n++)
                    _i[n] = Integer.parseInt(s[n]);
                break;
            case 'f':
                _f = new float[s.length];
                for (int n = 0; n < s.length; n++)
                    _f[n] = Float.parseFloat(s[n]);
                break;
            case 'd':
                _d = new double[s.length];
                for (int n = 0; n < s.length; n++)
                    _d[n] = Double.parseDouble(s[n]);
                break;
            case 's':
                _s = s.clone();
                break;
            default:
                throw new Exception("Cannot cast String to requested type.");
        }
        type = _type;
        length = s.length;
    }

    public void setTimeTag(TimeTag tt) {
        if (tt != null) {
            timetag.sec = tt.sec;
            timetag.frac = tt.frac;
        }
    }

    public String toString() {
        String s;
        switch (type) {
            case 'b': s = Arrays.toString(_b); break;
            case 'i': s = Arrays.toString(_i); break;
            case 'f': s = Arrays.toString(_f); break;
            case 'd': s = Arrays.toString(_d); break;
            case 's': s = Arrays.toString(_s); break;
            default: s = null;
        }
        return "<type="+type+", length="+length+", value="+s+">";
    }

    public char type;
    public int length;

    private boolean[] _b;
    private int[] _i;
    private float[] _f;
    private double[] _d;
    private String[] _s;
    public TimeTag timetag = new TimeTag(0, 1);
}
