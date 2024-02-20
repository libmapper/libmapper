using System;
using Mapper;

public class Test
{
    private static void GraphHandler(Mapper.Object obj, Mapper.Graph.Event evt)
    {
        Console.WriteLine("Graph received event " + obj + ": " + evt);
    }

    private static void SignalHandler(Signal sig, Signal.Event evt, float value, Time time)
    {
        Console.WriteLine("Signal received value " + value);
    }

    public static void Main(string[] args)
    {
        Graph graph = new Graph();
        Console.WriteLine("created Graph");
        graph.AddCallback(GraphHandler, Mapper.Type.Signal | Mapper.Type.Map);

        Device dev = new Device("csharp.test");

        int[] min = {1,2,3,4}, max = {10,11,12,13};
        Signal outsig = dev.AddSignal(Signal.Direction.Outgoing, "outsig", 1, Mapper.Type.Float)
                           .SetProperty(Property.Min, min)
                           .SetProperty(Property.Max, max);
        Console.WriteLine("created signal outsig");

        Signal insig = dev.AddSignal(Signal.Direction.Incoming, "insig", 1, Mapper.Type.Float)
                          .SetCallback((Action<Signal, Signal.Event, float, Time>)SignalHandler,
                                       Mapper.Signal.Event.Update);
        Console.WriteLine("created Signal insig");

        Console.Write("Waiting for Device...");
        while (dev.GetIsReady() == 0)
        {
            dev.Poll(25);
            graph.Poll();
        }
        Console.WriteLine("ready: " + dev);

        dev.SetProperty("foo", 1000, false);
        Console.WriteLine("property 'foo' = " + dev.GetProperty("foo"));
        Console.WriteLine("library version = " + dev.GetProperty(Property.LibVersion));

        for (int i = 0; i < dev.GetNumProperties(); i++)
            Console.WriteLine("property["+i+"] = " + dev.GetProperty(i));

        Console.WriteLine("Signals:");
        Mapper.List<Signal> sigs = dev.GetSignals();
        foreach(Signal s in sigs) { Console.WriteLine("  " + s); };

        // This constructor doesn't work properly on Apple Silicon
        // Map map = new Map("%y=%x*1000", insig, outsig);

        // Use the simple constructor + property setter instead
        Map map = new Map(outsig, insig).SetProperty(Property.Expression, "y=x*1000");
        map.Push();

        Console.Write("Waiting for Map...");
        while (map.GetIsReady() == 0)
        {
            dev.Poll(25);
            graph.Poll();
        }
        Console.WriteLine("ready!");

        float sig_val = 0.0F;
        int counter = 0;
        while (++counter < 100)
        {
            outsig.SetValue(sig_val);
            dev.Poll(100);
            graph.Poll();
            sig_val += 0.1F;
            if (sig_val > 100)
                sig_val = 0.0F;
            Console.Write("Signal updated to ");
            Console.WriteLine(sig_val.ToString());
        }

        Console.WriteLine("Testing Time class:");
        Time t = new Time();
        Console.WriteLine("  " + t);
        t.SetDouble(1234.5678);
        Console.WriteLine("  " + t);
    }
}
