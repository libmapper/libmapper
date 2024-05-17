﻿using Mapper.NET;
using Type = Mapper.NET.Type;

namespace Demo;

class Program
{
    static void Main(string[] args)
    {
        var dev = new Device("CSharpDemo");
        while (true)
        {
            if (dev.Ready) break;
            dev.Poll(100);
        }
        
        var sigA = dev.AddSignal(Signal.Direction.Outgoing, "Sine", 1, Type.Double);
        var sigB = dev.AddSignal(Signal.Direction.Incoming, "Debug_Log", 1, Type.Double);
        
        var map = new Map(sigA, sigB);
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
            var received = sigB.GetValue();
            Console.WriteLine($"Received: {received}");
        }
    }
}