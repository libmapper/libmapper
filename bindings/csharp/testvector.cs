using System;
using Mapper;

public class TestVector
{
    private static void SignalHandler(Signal sig, Mapper.Signal.Event evt, float[] value, Time time)
    {
        Console.WriteLine("Signal received value [" + String.Join(",", value) + "]");
    }

    public static void Main(string[] args)
    {
        Device dev = new Device("csharp.testvector");

        int[] min = {1,2,3,4}, max = {10,11,12,13};
        Signal outsig = dev.AddSignal(Signal.Direction.Outgoing, "outsig", 4, Mapper.Type.Float)
                           .SetProperty(Property.Min, min)
                           .SetProperty(Property.Max, max);
        Console.WriteLine("created signal outsig");

        Signal insig = dev.AddSignal(Signal.Direction.Incoming, "insig", 4, Mapper.Type.Float)
                          .SetCallback((Action<Signal, Signal.Event, float[], Time>)SignalHandler,
                                       Mapper.Signal.Event.Update);
        Console.WriteLine("created Signal insig");

        Console.Write("Waiting for Device...");
        while (dev.GetIsReady() == 0)
        {
            dev.Poll(25);
        }
        Console.WriteLine("ready: " + dev);

        Map map = new Map(outsig, insig);
        map.Push();

        Console.Write("Waiting for Map...");
        while (map.GetIsReady() == 0)
        {
            dev.Poll(25);
        }
        Console.WriteLine("ready!");

        float[] sig_val = {0.0F, 1.0F, 2.0F, 3.0F};
        int counter = 0;
        while (++counter < 100)
        {
            outsig.SetValue(sig_val);
            dev.Poll(100);
            for (int i = 0; i < 4; i++)
                sig_val[i] *= 1.1F;
            Console.WriteLine("Signal updated to [" + String.Join(",", sig_val) + "]");
        }
    }
}
