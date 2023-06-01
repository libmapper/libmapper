
package mapper;

public class Time
{
    public static final Time NOW = new Time();

    private native long mapperTime();
    private native long mapperTimeFromDouble(double time);

    public Time()
    {
        _time = mapperTime();
    }
    public Time(long time)
    {
        _time = time;
    }
    public Time(double time)
    {
        _time = mapperTimeFromDouble(time);
    }

    public Time now()
    {
        _time = mapperTime();
        return this;
    }

    public native Time add(Time addend);
    public native Time addDouble(double addend);
    public native Time subtract(Time subtrahend);
    public native Time multiply(double multiplicand);

    public native double getDouble();
    public Time setDouble(double time)
    {
        _time = mapperTimeFromDouble(time);
        return this;
    }

    public native boolean isAfter(Time time);
    public native boolean isBefore(Time time);

    public String toString()
    {
        return Double.toString(this.getDouble());
    }

    private long _time;
}
