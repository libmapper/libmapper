using System;
using System.Threading;
using Mapper;

public class TestCSharp
{
    private static void graph_handler(Mapper.Object obj, Mapper.Graph.Event evt) {
        Console.WriteLine("Graph received event " + obj + ": " + evt);
    }

    private static void sig_handler(Signal sig, Mapper.Signal.Event evt, float value) {
        Console.WriteLine("Signal received value " + value);
    }

    public static void Main(string[] args) {
        // string version = Mapper.getVersion();
        // Console.WriteLine("mapper version = " + version);

        Graph graph = new Graph();
        Console.WriteLine("created Graph");
        graph.addCallback(graph_handler, (int)Mapper.Type.Signal | (int)Mapper.Type.Map);

        Device dev = new Device("CSmapper");
        Console.WriteLine("created Device CSmapper");

        int[] min = {1,2,3,4}, max = {10,11,12,13};
        Mapper.Signal outsig = dev.addSignal(Direction.Outgoing, "outsig", 1, Mapper.Type.Float)
                                  .setProperty(Property.Min, min)
                                  .setProperty(Property.Max, max);
        Console.WriteLine("created signal outsig");

        Signal insig = dev.addSignal(Direction.Incoming, "insig", 1, Mapper.Type.Float)
                          .setCallback((Action<Signal, Signal.Event, float>)sig_handler, (int)Mapper.Signal.Event.Update);
        Console.WriteLine("created Signal insig");

        Console.Write("Waiting for device");
        while (dev.getIsReady() == 0) {
            dev.poll(25);
            graph.poll();
        }
        Console.WriteLine("Device ready...");

        dev.setProperty("foo", 1000);
        Console.WriteLine("property 'foo' = " + dev.getProperty("foo"));

        // Map map = new Map(outsig, insig);
        Map map = new Map("%y=%x*1000", insig, outsig);
        map.push();

        Console.Write("Waiting for Map...");
        while (map.getIsReady() == 0) {
            dev.poll(25);
            graph.poll();
        }

        float sig_val = 0.0F;
        int counter = 0;
        while (++counter < 100) {
            outsig.setValue(sig_val);
            dev.poll(100);
            graph.poll();
            sig_val += 0.1F;
            if (sig_val > 100)
                sig_val = 0.0F;
            Console.Write("Signal updated to ");
            Console.WriteLine(sig_val.ToString());
        }
    }
}
