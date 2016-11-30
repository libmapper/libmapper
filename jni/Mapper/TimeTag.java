
package mapper;

public class TimeTag
{
    public long sec;
    public long frac;

    public static final TimeTag NOW = new TimeTag(0, 1);
    private static double multiplier = (double)1.0/((double)((long)1<<32));

    private native void mapperNow();

    public TimeTag(long _sec, long _frac)
    {
        sec = _sec;
        frac = _frac;
    }
    public TimeTag()
    {
        mapperNow();
    }

    public TimeTag(Double secondsSinceEpoch)
    {
        sec = (long)Math.floor(secondsSinceEpoch);
        secondsSinceEpoch -= sec;
        frac = (long)(secondsSinceEpoch*(double)((long)1<<32));
    }

    public TimeTag now()
    {
        mapperNow();
        return this;
    }

    public double getDouble()
    {
        return (double)sec + (double)frac * multiplier;
    }

    public boolean isAfter(TimeTag rhs)
    {
        return (sec > rhs.sec || (sec == rhs.sec && frac > rhs.frac));
    }

    public boolean isBefore(TimeTag rhs)
    {
        return (sec < rhs.sec || (sec == rhs.sec && frac < rhs.frac));
    }

    public String toString()
    {
        return sec + "." + frac;
    }
}
