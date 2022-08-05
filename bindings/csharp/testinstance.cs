using System;
using System.Threading;
using Mapper;

public class TestCSharpInstances
{
    private static void handler(Signal.Instance inst, Mapper.Signal.Event evt, float value) {
        Console.Write("handler: " + evt + " " + inst.getProperty(Property.Name) + "." + inst.id);
        switch (evt) {
            case Mapper.Signal.Event.Update:
                Console.Write(": " + value);
                break;
            case Mapper.Signal.Event.UpstreamRelease:
                inst.release();
                break;
        }
        Console.WriteLine();
    }

    public static void Main(string[] args) {
        // string version = Mapper.getVersion();
        // Console.WriteLine("mapper version = " + version);

        Device dev = new Device("CSmapper");
        Console.WriteLine("created dev CSmapper");

        Signal outsig = dev.addSignal(Direction.Outgoing, "outsig", 1, Mapper.Type.Float, null, 3);
        Console.WriteLine("created signal outsig");

        Signal insig = dev.addSignal(Direction.Incoming, "insig", 1, Mapper.Type.Float, null, 3)
                          .setCallback((Action<Signal.Instance, Signal.Event, float>)handler);
        Console.WriteLine("created signal insig");

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

        float sig_val = 0.0F;
        int counter = 0;
        while (++counter < 100) {
            outsig.instance(counter % 3).setValue(sig_val);
            dev.poll(100);
            sig_val += 10F;
            if (sig_val > 100) {
                outsig.instance(counter % 3).release();
                sig_val = 0.0F;
            }
            Console.Write("outsig instance " + counter % 3 + " updated to ");
            Console.WriteLine(sig_val.ToString());
        }
    }
}
