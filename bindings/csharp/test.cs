using System;
using System.Threading;
using Mapper;

public class Test
{
    private static void GraphHandler(Mapper.Object obj, Mapper.Graph.Event evt)
    {
        Console.WriteLine("Graph received event " + obj + ": " + evt);
    }

    private static void SignalHandler(Signal sig, Mapper.Signal.Event evt, float value)
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
        Mapper.Signal outsig = dev.AddSignal(Direction.Outgoing, "outsig", 1, Mapper.Type.Float)
                                  .SetProperty(Property.Min, min)
                                  .SetProperty(Property.Max, max);
        Console.WriteLine("created signal outsig");

        Signal insig = dev.AddSignal(Direction.Incoming, "insig", 1, Mapper.Type.Float)
                          .SetCallback((Action<Signal, Signal.Event, float>)SignalHandler,
                                       Mapper.Signal.Event.Update);
        Console.WriteLine("created Signal insig");

        Console.Write("Waiting for Device...");
        while (dev.GetIsReady() == 0)
        {
            dev.Poll(25);
            graph.Poll();
        }
        Console.WriteLine("ready: " + dev);

        dev.SetProperty("foo", 1000);
        Console.WriteLine("property 'foo' = " + dev.GetProperty("foo"));
        Console.WriteLine("library version = " + dev.GetProperty(Property.LibVersion));

        for (int i = 0; i < dev.GetNumProperties(); i++)
            Console.WriteLine("property["+i+"] = " + dev.GetProperty(i));

        Console.WriteLine("Signals:");
        Mapper.List<Signal> sigs = dev.GetSignals();
        foreach(Signal s in sigs) { Console.WriteLine("  " + s); };

        // Map map = new Map(outsig, insig);
        Map map = new Map("%y=%x*1000", insig, outsig);
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
