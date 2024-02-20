using System;
using Mapper;

public class TestInstances
{
    private static void Handler(Signal.Instance inst, Mapper.Signal.Event evt, float value, Time time)
    {
        Console.Write("handler: " + evt + " " + inst.GetProperty(Property.Name) + "." + inst.id);
        switch (evt)
        {
            case Mapper.Signal.Event.Update:
                Console.Write(": " + value);
                break;
            case Mapper.Signal.Event.UpstreamRelease:
                inst.Release();
                break;
        }
        Console.WriteLine();
    }

    public static void Main(string[] args)
    {
        Device dev = new Device("csharp.testinstance");

        Signal outsig = dev.AddSignal(Signal.Direction.Outgoing, "outsig", 1, Mapper.Type.Float, null, 3);
        Console.WriteLine("created signal outsig");

        Signal insig = dev.AddSignal(Signal.Direction.Incoming, "insig", 1, Mapper.Type.Float, null, 3)
                          .SetCallback((Action<Signal.Instance, Signal.Event, float, Time>)Handler);
        Console.WriteLine("created signal insig");

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

        float sig_val = 0.0F;
        int counter = 0;
        while (++counter < 100)
        {
            outsig.GetInstance(counter % 3).SetValue(sig_val);
            dev.Poll(100);
            sig_val += 10F;
            if (sig_val > 100)
            {
                outsig.GetInstance(counter % 3).Release();
                sig_val = 0.0F;
            }
            Console.Write("outsig instance " + counter % 3 + " updated to ");
            Console.WriteLine(sig_val.ToString());
        }
    }
}
