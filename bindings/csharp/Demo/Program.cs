using Mapper;

namespace Demo;

internal static class Program
{
    private static void Main(string[] args)
    {
        var sent = 0;
        var count = 100;
        var dev = new Device("CSharpDemo");
        while (true)
        {
            if (dev.Ready) break;
            dev.Poll(100);
        }

        var sigA = dev.AddSignal(Signal.Direction.Outgoing, "Sine", 1, MapperType.Double);
        sigA.ReserveInstances(10);
        var sigB = dev.AddSignal(Signal.Direction.Incoming, "Debug_Log", 1, MapperType.Double);
        sigB.ReserveInstances(10);
        sigB.ValueChanged += OnEvent;

        sigA.SetProperty(Property.Ephemeral, true);
        sigB.SetProperty(Property.Ephemeral, true);

        var map = new Map("%y=2.0*%x", sigB, sigA);
        map.Push();
        while (!map.IsReady)
        {
            dev.Poll(100);
        }

        Console.WriteLine("Map created SigA -> SigB");

        var rand = new Random();
        while (count-- > 0)
        {
            ulong instanceId = Convert.ToUInt64(rand.Next(10));
            if (rand.Next(3) > 0) {
                var value = Math.Sin(DateTime.Now.Second / 10.0);
                sigA.SetValue(value, instanceId);
                ++sent;
                Console.WriteLine($"Sent: {instanceId}, {value}, {sent}");
            }
            else {
                sigA.Release(instanceId);
                Console.WriteLine($"Released: {instanceId}");
            }
            var numInstances = sigA.GetNumInstances(Signal.Status.Any);
            Console.WriteLine($"{(int)sigA.GetNumInstances(Signal.Status.Active)}/{numInstances}Active Instances:");
            for (int i = 0; i < numInstances; i++) {
                var instance = sigA.GetInstance(i);
                Console.WriteLine($"  {i}) ID: {instance.id}, STATUS: {instance.GetStatus()}");
            }
            dev.Poll(100);
        }

        var rcvd = sigB.GetProperty("rcvd");
        Console.WriteLine($"Sent {sent} and received {rcvd}");

        dev = null;
    }

    private static void OnEvent(object? sender, (Signal.Event eventType, ulong instanceId, object? value, MapperType objectType, Time changed) data)
    {
        if (sender == null)
            return;
        Console.WriteLine($"Received: {(Signal)sender}, {data.eventType}, {data.instanceId}, {data.value}");
        if (data.eventType == Signal.Event.UpstreamRelease) {
            Console.WriteLine($"Releasing: {(Signal)sender}, {data.instanceId}");
            ((Signal)sender).Release(data.instanceId);
        }
        else if (data.eventType == Signal.Event.Update) {
            var rcvdProp = ((Signal)sender).GetProperty("rcvd");
            int rcvd = 0;
            if (rcvdProp != null)
                rcvd = (int)rcvdProp;
            rcvd += 1;
            ((Signal)sender).SetProperty("rcvd", rcvd);
        }
        var numInstances = ((Signal)sender).GetNumInstances(Signal.Status.Any);
        Console.WriteLine($"{(int)((Signal)sender).GetNumInstances(Signal.Status.Active)}/{numInstances}Active Instances:");
        for (int i = 0; i < numInstances; i++) {
            var instance = ((Signal)sender).GetInstance(i);
            Console.WriteLine($"  {i}) ID: {instance.id}, STATUS: {instance.GetStatus()}");
        }
    }
}