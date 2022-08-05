using System;
using System.Threading;
using Mapper;

public class TestCSharpVector
{
    private static void sig_handler(Signal sig, Mapper.Signal.Event evt, float[] value) {
        Console.WriteLine("Signal received value [" + String.Join(",", value) + "]");
    }

    public static void Main(string[] args) {
        Device dev = new Device("csharp.testvector");

        int[] min = {1,2,3,4}, max = {10,11,12,13};
        Mapper.Signal outsig = dev.addSignal(Direction.Outgoing, "outsig", 4, Mapper.Type.Float)
                                  .setProperty(Property.Min, min)
                                  .setProperty(Property.Max, max);
        Console.WriteLine("created signal outsig");

        Signal insig = dev.addSignal(Direction.Incoming, "insig", 4, Mapper.Type.Float)
                          .setCallback((Action<Signal, Signal.Event, float[]>)sig_handler,
                                       Mapper.Signal.Event.Update);
        Console.WriteLine("created Signal insig");

        Console.Write("Waiting for Device...");
        while (dev.getIsReady() == 0) {
            dev.poll(25);
        }
        Console.WriteLine("ready!");

        Map map = new Map(outsig, insig);
        map.push();

        Console.Write("Waiting for Map...");
        while (map.getIsReady() == 0) {
            dev.poll(25);
        }
        Console.WriteLine("ready!");

        float[] sig_val = {0.0F, 1.0F, 2.0F, 3.0F};
        int counter = 0;
        while (++counter < 100) {
            outsig.setValue(sig_val);
            dev.poll(100);
            for (int i = 0; i < 4; i++)
                sig_val[i] *= 1.1F;
            Console.WriteLine("Signal updated to [" + String.Join(",", sig_val) + "]");
        }
    }
}
