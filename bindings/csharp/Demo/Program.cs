using Mapper;

namespace Demo;

internal static class Program
{
    private static void Main(string[] args)
    {
        var dev = new Device("CSharpDemo");
        while (true)
        {
            if (dev.Ready) break;
            dev.Poll(100);
        }
        
        var sigA = dev.AddSignal(Signal.Direction.Outgoing, "Sine", 1, MapperType.Double);
        var sigB = dev.AddSignal(Signal.Direction.Incoming, "Debug_Log", 1, MapperType.Double);
        sigB.ValueChanged += OnEvent;
        
        var map = new Map("%y=2.0*%x", sigB, sigA);
        map.Push();
        while (!dev.Ready)
        {
            dev.Poll(100);
        }
        
        Console.WriteLine("Map created SigA -> SigB");

        while (true)
        {
            var value = Math.Sin(DateTime.Now.Second / 10.0);
            sigA.SetValue(value);
            Console.WriteLine($"Sent: {value}");
            dev.Poll(500);
        }

    }
    
    private static void OnEvent(object? sender, (ulong instanceId, object? value, MapperType objectType, Time changed) data)
    {
        Console.WriteLine($"Received: {data.value}");
    }
}