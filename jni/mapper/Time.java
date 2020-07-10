
package mapper;

public class Time
{
    public int sec;
    public int frac;

    public static final Time NOW = new Time(0, 1);
    private static double multiplier = (double)1.0/((double)((long)1<<32));

    private native void mapperNow();

    public Time(int _sec, int _frac)
    {
        sec = _sec;
        frac = _frac;
    }
    public Time()
    {
        mapperNow();
    }

    public Time(Double secondsSinceEpoch)
    {
        sec = (int)Math.floor(secondsSinceEpoch);
        secondsSinceEpoch -= sec;
        frac = (int)(secondsSinceEpoch*(double)((long)1<<32));
    }

    public Time now()
    {
        mapperNow();
        return this;
    }

    public double getDouble()
    {
        return (double)sec + (double)frac * multiplier;
    }

    public boolean isAfter(Time rhs)
    {
        return (sec > rhs.sec || (sec == rhs.sec && frac > rhs.frac));
    }

    public boolean isBefore(Time rhs)
    {
        return (sec < rhs.sec || (sec == rhs.sec && frac < rhs.frac));
    }

    public String toString()
    {
        return Double.toString(this.getDouble());
    }
}
