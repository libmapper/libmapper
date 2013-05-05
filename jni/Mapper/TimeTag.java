
package Mapper;

public class TimeTag
{
    public long sec;
    public long frac;

    public static final TimeTag NOW = new TimeTag(0, 1);

    public TimeTag(long _sec, long _frac)
    {
        sec = _sec;
        frac = _frac;
    }

    public TimeTag(Double secondsSinceEpoch)
    {
        // TO DO
    }
}
