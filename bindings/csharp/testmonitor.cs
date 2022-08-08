using System;
using System.Threading;
using Mapper;

public class TestMonitor
{
    static int update = -100;
    static bool verbose = true;
    static bool terminate = true;
    static volatile bool done = false;

    private static void OnEvent(Mapper.Object obj, Mapper.Graph.Event evt)
    {
        if (verbose)
            Console.WriteLine(evt + ": " + obj);
        update = 1;
    }

    public static void Main(string[] args)
    {
        int polltime_ms = 100;
        int i = 0;

        Graph graph = new Graph().AddCallback(OnEvent);

        Console.CancelKeyPress += delegate(object sender, ConsoleCancelEventArgs e)
        {
            e.Cancel = true;
            TestMonitor.done = true;
        };

        while ((!terminate || i++ < 250) && !done)
        {
            graph.Poll(polltime_ms);

            if (update++ < 0)
                continue;
            update = -100;

            if (verbose) {
                Console.Clear();

                List<Device> devices = graph.GetDevices();

                Console.WriteLine("-------------------------------");
                Console.WriteLine("Registered devices (" + devices.Count() +
                                  ") and signals (" + graph.GetSignals().Count() + "):");

                foreach(Device d in devices)
                {
                    Console.WriteLine(" └─ " + d);
                    List<Signal> signals = d.GetSignals();
                    int j = 1, last = signals.Count();
                    foreach(Signal s in signals)
                    {
                        if (j < last)
                            Console.WriteLine("    ├─ " + s);
                        else
                            Console.WriteLine("    └─ " + s);
                        ++j;
                    }
                }

                List<Map> maps = graph.GetMaps();
                Console.WriteLine("-------------------------------");
                Console.WriteLine("Registered maps (" + maps.Count() + "):");

                foreach(Map m in maps)
                {
                    Console.WriteLine("└─ " + m);
                    List<Signal> signals = m.GetSignals(Mapper.Map.Location.Source);
                    foreach(Signal s in signals)
                    {
                        Console.WriteLine("    ├─ SRC " + s);
                    }
                    signals = m.GetSignals(Mapper.Map.Location.Destination);
                    foreach(Signal s in signals)
                    {
                        Console.WriteLine("    └─ DST " + s);
                    }
                }

                Console.WriteLine("-------------------------------");
            }
            else
            {
                Console.Clear();
                Console.WriteLine(graph.GetInterface() + " ",
                                  "Devices: " + graph.GetDevices().Count() + ", ",
                                  "Signals: " + graph.GetSignals().Count() + ", ",
                                  "Maps: " + graph.GetMaps().Count());
            }
        }
    }
}
