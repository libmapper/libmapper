using System.Runtime.InteropServices;

namespace Mapper.NET;

public class Time
{
    internal timeStruct data;

    public Time(long ntp)
    {
        data.ntp = ntp;
    }

    public Time(Time time)
    {
        data.ntp = time.data.ntp;
    }

    public Time()
    {
        data.sec = 0;
        data.frac = 1;
    }

    public Time(double seconds)
    {
        SetDouble(seconds);
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern int mpr_time_set(IntPtr l, long r);

    public unsafe Time Set(Time time)
    {
        fixed (void* t = &data)
        {
            mpr_time_set((IntPtr)t, time.data.ntp);
        }

        return this;
    }

    public Time SetDouble(double seconds)
    {
        if (seconds > 0.0)
        {
            data.sec = (uint)Math.Floor(seconds);
            seconds -= data.sec;
            data.frac = (uint)(seconds * 4294967296.0);
        }
        else
        {
            data.ntp = 0;
        }

        return this;
    }

    private static double as_dbl(Time time)
    {
        return time.data.sec + time.data.frac * 0.00000000023283064365;
    }

    public Time Add(Time addend)
    {
        data.sec += addend.data.sec;
        data.frac += addend.data.frac;
        if (data.frac < addend.data.frac) /* overflow */
            ++data.sec;
        return this;
    }

    public Time Subtract(Time subtrahend)
    {
        if (data.sec > subtrahend.data.sec)
        {
            data.sec -= subtrahend.data.sec;
            if (data.frac < subtrahend.data.frac) /* overflow */
                --data.sec;
            data.frac -= subtrahend.data.frac;
        }
        else
        {
            data.ntp = 0;
        }

        return this;
    }

    public Time Multiply(double multiplicand)
    {
        if (multiplicand > 0.0)
        {
            multiplicand *= as_dbl(this);
            data.sec = (uint)Math.Floor(multiplicand);
            multiplicand -= data.sec;
            data.frac = (uint)(multiplicand * 4294967296.0);
        }
        else
        {
            data.ntp = 0;
        }

        return this;
    }

    /* casting between Time and double */
    public static implicit operator double(Time t)
    {
        return as_dbl(t);
    }

    public static explicit operator Time(double d)
    {
        return new Time(d);
    }

    /* Overload some arithmetic operators */
    public static Time operator +(Time a, Time b)
    {
        return new Time(a).Add(b);
    }

    public static Time operator -(Time a, Time b)
    {
        return new Time(a).Subtract(b);
    }

    public static Time operator *(Time a, double b)
    {
        return new Time(a).Multiply(b);
    }

    public static bool operator ==(Time a, Time b)
    {
        return a.data.ntp == b.data.ntp;
    }

    public static bool operator !=(Time a, Time b)
    {
        return a.data.ntp != b.data.ntp;
    }

    public static bool operator >(Time a, Time b)
    {
        return a.data.ntp > b.data.ntp;
    }

    public static bool operator <(Time a, Time b)
    {
        return a.data.ntp < b.data.ntp;
    }

    public static bool operator >=(Time a, Time b)
    {
        return a.data.ntp >= b.data.ntp;
    }

    public static bool operator <=(Time a, Time b)
    {
        return a.data.ntp <= b.data.ntp;
    }

    public override bool Equals(object? o)
    {
        if (o == null)
            return false;
        var second = o as Time;
        return data.ntp == second!.data.ntp;
    }

    public override int GetHashCode()
    {
        return (int)(data.sec ^ data.frac);
    }

    public override string ToString()
    {
        return $"Mapper.Time:{data.sec}:{data.frac}";
    }
    // internal long _time;

    [StructLayout(LayoutKind.Explicit)]
    internal struct timeStruct
    {
        [FieldOffset(0)] internal long ntp;
        [FieldOffset(0)] internal UInt32 sec;
        [FieldOffset(4)] internal UInt32 frac;
    }
}