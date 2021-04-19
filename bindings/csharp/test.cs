using System;
using System.Threading;
using Mapper;

public class TestCSharp
{
    private static void handler(Signal sig, Mapper.Signal.Event evt, UInt64 instance, int length,
                                Mapper.Type type, IntPtr value, IntPtr time) {
        unsafe {
            float fvalue = *(float*)value;
            Console.WriteLine("received value " + fvalue);
        }
    }

    public static void Main(string[] args) {
        // string version = Mapper.getVersion();
        // Console.WriteLine("mapper version = " + version);

        Device dev = new Device("CSmapper");
        Console.WriteLine("created dev CSmapper");

        int[] min = {1,2,3,4};
        int[] max = {10,11,12,13};
        Mapper.Signal outsig = dev.addSignal(Direction.Outgoing, "outsig", 1, Mapper.Type.Float);
        Console.WriteLine("created signal outsig");

        Signal insig = dev.addSignal(Direction.Incoming, "insig", 1, Mapper.Type.Float, null,
                                     IntPtr.Zero, IntPtr.Zero, IntPtr.Zero)
                          .setCallback(handler, (int)Mapper.Signal.Event.Update);
        Console.WriteLine("created signal insig");

        Console.Write("Waiting for device");
        while (dev.getIsReady() == 0) {
            dev.poll(25);
        }
        Console.WriteLine("Device ready...");

        // Map map = new Map(outsig, insig);
        Map map = new Map("%y=%x*1000", insig, outsig);
        map.push();

        Console.Write("Waiting for map...");
        while (map.getIsReady() == 0) {
            dev.poll(25);
        }

        float sig_val = 0.0F;
        int counter = 0;
        while (++counter < 100) {
            outsig.setValue(sig_val);
            dev.poll(100);
            sig_val += 0.1F;
            if (sig_val > 100)
              sig_val = 0F;
            Console.Write("Sig updated to ");
            Console.WriteLine(sig_val.ToString());
        }
    }
}
