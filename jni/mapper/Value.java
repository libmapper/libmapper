
package mapper;
import mapper.TimeTag;
import java.util.Arrays;
import mapper.Type;

/* Contains a value representable by supported types, which correspond
 * with OSC types. */
public class Value
{
    public Value() {
        length = 0;
        type = Type.NULL;
        _type = (char)type.value();
    }
    public Value(boolean b)             { set(Type.BOOL, b);   }
    public Value(int i)                 { set(Type.INT, i);   }
    public Value(float f)               { set(Type.FLOAT, f);   }
    public Value(double d)              { set(Type.DOUBLE, d);   }
    public Value(String s)              { set(Type.STRING, s);   }
    public Value(boolean[] b)           { set(Type.BOOL, b);   }
    public Value(int[] i)               { set(Type.INT, i);   }
    public Value(float[] f)             { set(Type.FLOAT, f);   }
    public Value(double[] d)            { set(Type.DOUBLE, d);   }
    public Value(String[] s)            { set(Type.STRING, s);   }
    public Value(TimeTag t)             { setTimeTag(t); }
    public Value(Type t, int i)         { set(t, i); }
    public Value(Type t, float f)       { set(t, f); }
    public Value(Type t, double d)      { set(t, d); }
    public Value(Type t, String s)      { set(t, s); }
    public Value(Type t, int[] i)       { set(t, i); }
    public Value(Type t, float[] f)     { set(t, f); }
    public Value(Type t, double[] d)    { set(t, d); }
    public Value(Type t, String[] s)    { set(t, s); }

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
        if (type == Type.BOOL) {
            if (length == 1)
                return new Boolean(_b[0]);
            else if (length > 1) {
                boolean[] b = _b.clone();
                return b;
            }
        }
        else if (type == Type.INT) {
            if (length == 1)
                return new Integer(_i[0]);
            else if (length > 1) {
                int[] v = _i.clone();
                return v;
            }
        }
        else if (type == Type.FLOAT) {
            if (length == 1)
                return new Float(_f[0]);
            else if (length > 1) {
                float[] v = _f.clone();
                return v;
            }
        }
        else if (type == Type.DOUBLE) {
            if (length == 1)
                return new Double(_d[0]);
            else if (length > 1) {
                double[] v = _d.clone();
                return v;
            }
        }
        else if (type == Type.STRING) {
            if (length == 1)
                return _s[0];
            else if (length > 1) {
                String[] v = _s.clone();
                return v;
            }
        }
        return null;
    }

    public boolean booleanValue() {
        if (type == Type.BOOL)
            return _b[0];
        throw new Exception();
    }

    public int intValue() {
        if (type == Type.INT)
            return _i[0];
        throw new Exception();
    }

    public float floatValue() {
        if (type == Type.FLOAT)
            return _f[0];
        else if (type == Type.INT)
            return _i[0];
        throw new Exception();
    }

    public double doubleValue() {
        if (type == Type.DOUBLE)
            return _d[0];
        else if (type == Type.FLOAT)
            return _f[0];
        else if (type == Type.INT)
            return _i[0];
        throw new Exception();
    }

    public String stringValue() {
        if (type == Type.STRING)
            return _s[0];
        throw new Exception();
    }

    public void set(Type t, boolean b) {
        if (t == Type.BOOL) {
            _b = new boolean[1];
            _b[0] = b;
        }
        else if (t == Type.INT) {
            _i = new int[1];
            _i[0] = b ? 1 : 0;
        }
        else if (t == Type.FLOAT) {
            _f = new float[1];
            _f[0] = b ? 1 : 0;
        }
        else if (t == Type.DOUBLE) {
            _d = new double[1];
            _d[0] = b ? 1 : 0;
        }
        else if (t == Type.STRING) {
            _s = new String[1];
            _s[0] = String.valueOf(b);
        }
        else {
            throw new Exception("Cannot cast boolean to requested type.");
        }
        type = t;
        _type = (char)type.value();
        length = 1;
    }

    public void set(Type t, int i) {
        if (t == Type.BOOL) {
            _b = new boolean[1];
            _b[0] = (i != 0);
        }
        else if (t == Type.INT) {
            _i = new int[1];
            _i[0] = i;
        }
        else if (t == Type.FLOAT) {
            _f = new float[1];
            _f[0] = i;
        }
        else if (t == Type.DOUBLE) {
            _d = new double[1];
            _d[0] = i;
        }
        else if (t == Type.STRING) {
            _s = new String[1];
            _s[0] = String.valueOf(i);
        }
        else {
            throw new Exception("Cannot cast int to requested type.");
        }
        type = t;
        _type = (char)type.value();
        length = 1;
    }

    public void set(Type t, float f) {
        if (t == Type.BOOL) {
            _b = new boolean[1];
            _b[0] = (f != 0.f);
        }
        else if (t == Type.INT) {
            _i = new int[1];
            _i[0] = (int)f;
        }
        else if (t == Type.FLOAT) {
            _f = new float[1];
            _f[0] = f;
        }
        else if (t == Type.DOUBLE) {
            _d = new double[1];
            _d[0] = f;
        }
        else if (t == Type.STRING) {
            _s = new String[1];
            _s[0] = String.valueOf(f);
        }
        else {
            throw new Exception("Cannot cast float to requested type.");
        }
        type = t;
        _type = (char)type.value();
        length = 1;
    }

    public void set(Type t, double d) {
        if (t == Type.BOOL) {
            _b = new boolean[1];
            _b[0] = (d != 0.0);
        }
        else if (t == Type.INT) {
            _i = new int[1];
            _i[0] = (int)d;
        }
        else if (t == Type.FLOAT) {
            _f = new float[1];
            _f[0] = (float)d;
        }
        else if (t == Type.DOUBLE) {
            _d = new double[1];
            _d[0] = d;
        }
        else if (t == Type.STRING) {
            _s = new String[1];
            _s[0] = String.valueOf(d);
        }
        else {
            throw new Exception("Cannot cast double to requested type.");
        }
        type = t;
        _type = (char)type.value();
        length = 1;
    }

    public void set(Type t, String s) {
        if (t == Type.BOOL) {
            _b = new boolean[1];
            _b[0] = Boolean.parseBoolean(s);
        }
        else if (t == Type.INT) {
            _i = new int[1];
            _i[0] = Integer.parseInt(s);
        }
        else if (t == Type.FLOAT) {
            _f = new float[1];
            _f[0] = Float.parseFloat(s);
        }
        else if (t == Type.DOUBLE) {
            _d = new double[1];
            _d[0] = Double.parseDouble(s);
        }
        else if (t == Type.STRING) {
            _s = new String[1];
            _s[0] = s;
        }
        else {
            throw new Exception("Cannot cast String to requested type.");
        }
        type = t;
        _type = (char)type.value();
        length = 1;
    }

    public void set(Type t, boolean[] b) {
        if (t == Type.BOOL) {
            _b = b.clone();
        }
        else if (t == Type.INT) {
            _i = new int[b.length];
            for (int n = 0; n < b.length; n++)
                _i[n] = b[n] ? 1 : 0;
        }
        else if (t == Type.FLOAT) {
            _f = new float[b.length];
            for (int n = 0; n < b.length; n++)
                _f[n] = b[n] ? 1.f : 0.f;
        }
        else if (t == Type.DOUBLE) {
            _d = new double[b.length];
            for (int n = 0; n < b.length; n++)
                _d[n] = b[n] ? 1.0 : 0.0;
        }
        else if (t == Type.STRING) {
            _s = new String[b.length];
            for (int n = 0; n < b.length; n++)
                _s[n] = String.valueOf(b[n]);
        }
        else {
            throw new Exception("Cannot cast int to requested type.");
        }
        type = t;
        _type = (char)type.value();
        length = b.length;
    }

    public void set(Type t, int[] i) {
        if (t == Type.BOOL) {
            _b = new boolean[i.length];
            for (int n = 0; n < i.length; n++)
                _b[0] = (i[n] != 0);
        }
        else if (t == Type.INT) {
            _i = i.clone();
        }
        else if (t == Type.FLOAT) {
            _f = new float[i.length];
            for (int n = 0; n < i.length; n++)
                _f[n] = i[n];
        }
        else if (t == Type.DOUBLE) {
            _d = new double[i.length];
            for (int n = 0; n < i.length; n++)
                _d[n] = i[n];
        }
        else if (t == Type.STRING) {
            _s = new String[i.length];
            for (int n = 0; n < i.length; n++)
                _s[n] = String.valueOf(i[n]);
        }
        else {
            throw new Exception("Cannot cast int to requested type.");
        }
        type = t;
        _type = (char)type.value();
        length = i.length;
    }

    public void set(Type t, float[] f) {
        if (t == Type.BOOL) {
            _b = new boolean[f.length];
            for (int n = 0; n < f.length; n++)
                _b[0] = (f[n] != 0.f);
        }
        else if (t == Type.INT) {
            _i = new int[f.length];
            for (int n = 0; n < f.length; n++)
                _i[n] = (int)f[n];
        }
        else if (t == Type.FLOAT) {
            _f = f.clone();
        }
        else if (t == Type.DOUBLE) {
            _d = new double[f.length];
            for (int n = 0; n < f.length; n++)
                _d[n] = f[n];
        }
        else if (t == Type.STRING) {
            _s = new String[f.length];
            for (int n = 0; n < f.length; n++)
                _s[n] = String.valueOf(f[n]);
        }
        else {
            throw new Exception("Cannot cast float to requested type.");
        }
        type = t;
        _type = (char)type.value();
        length = f.length;
    }

    public void set(Type t, double[] d) {
        if (t == Type.BOOL) {
            _b = new boolean[d.length];
            for (int n = 0; n < d.length; n++)
                _b[0] = (d[n] != 0.0);
        }
        else if (t == Type.INT) {
            _i = new int[d.length];
            for (int n = 0; n < d.length; n++)
                _i[n] = (int)d[n];
        }
        else if (t == Type.FLOAT) {
            _f = new float[d.length];
            for (int n = 0; n < d.length; n++)
                _f[n] = (float)d[n];
        }
        else if (t == Type.DOUBLE) {
            _d = d.clone();
        }
        else if (t == Type.STRING) {
            _s = new String[d.length];
            for (int n = 0; n < d.length; n++)
                _s[n] = String.valueOf(d[n]);
        }
        else {
            throw new Exception("Cannot cast double to requested type.");
        }
        type = t;
        _type = (char)type.value();
        length = d.length;
    }

    public void set(Type t, String[] s) {
        if (t == Type.BOOL) {
            _b = new boolean[s.length];
            for (int n = 0; n < s.length; n++)
                _b[0] = Boolean.parseBoolean(s[n]);
        }
        else if (t == Type.INT) {
            _i = new int[s.length];
            for (int n = 0; n < s.length; n++)
                _i[n] = Integer.parseInt(s[n]);
        }
        else if (t == Type.FLOAT) {
            _f = new float[s.length];
            for (int n = 0; n < s.length; n++)
                _f[n] = Float.parseFloat(s[n]);
        }
        else if (t == Type.DOUBLE) {
            _d = new double[s.length];
            for (int n = 0; n < s.length; n++)
                _d[n] = Double.parseDouble(s[n]);
        }
        else if (t == Type.STRING) {
            _s = s.clone();
        }
        else {
            throw new Exception("Cannot cast String to requested type.");
        }
        type = t;
        _type = (char)type.value();
        length = s.length;
    }

    public void setTimeTag(TimeTag tt) {
        if (tt != null) {
            timetag.sec = tt.sec;
            timetag.frac = tt.frac;
            type = Type.TIMETAG;
            _type = (char)type.value();
        }
    }

    public String toString() {
        String s;
        if (type == Type.BOOL)
            s = Arrays.toString(_b);
        else if (type == Type.INT)
            s = Arrays.toString(_i);
        else if (type == Type.FLOAT)
            s = Arrays.toString(_f);
        else if (type == Type.DOUBLE)
            s = Arrays.toString(_d);
        else if (type == Type.STRING)
            s = Arrays.toString(_s);
        else
            s = null;
        return "<type="+type+", length="+length+", value="+s+">";
    }

    public Type type;
    public int length;

    char _type;
    private boolean[] _b;
    private int[] _i;
    private float[] _f;
    private double[] _d;
    private String[] _s;
    public TimeTag timetag = new TimeTag(0, 1);
}
