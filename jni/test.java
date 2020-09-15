
import mapper.*;
import mapper.graph.*;
import mapper.signal.*;
import mapper.map.*;
import java.util.Arrays;
import java.util.Iterator;

class test {
    public static void main(String [] args) {
        final Device dev1 = new Device("java_test");
        final Device dev2 = new Device("java_test");
        final Graph g = new Graph(Type.OBJECT);
        Time start = new Time();
        System.out.println("Current time: "+start.now());

        // This is how to ensure the device is freed when the program
        // exits, even on SIGINT.  The Device must be declared "final".
        Runtime.getRuntime().addShutdownHook(new Thread() {
                @Override
                public void run() {
                        dev1.free();
                        dev2.free();
                        g.free();
                    }
            });

        g.addListener(new mapper.graph.Listener<Device>() {
            public void onEvent(Device dev, mapper.graph.Event event) {
                Device.Properties p = dev.properties();
                System.out.println("graph record "+event+" for device "+p.get("name"));
                int numProps = p.size();
                for (int i = 0; i < numProps; i++) {
                    Device.Properties.Entry e = p.getEntry(i);
                    System.out.println("  " + e.getKey() + ": " + e.getValue());
                }
            }});

        g.addListener(new mapper.graph.Listener<Signal>() {
            public void onEvent(Signal sig, mapper.graph.Event event) {
                System.out.println("Graph evnt signal");
                Signal.Properties ps = sig.properties();
                Device.Properties pd = sig.device().properties();
                System.out.println("graph record "+event+" for signal "
                                   +pd.get("name")+":"+ps.get("name"));
                int numProps = ps.size();
                for (int i = 0; i < numProps; i++) {
                    Signal.Properties.Entry e = ps.getEntry(i);
                    System.out.println("  " + e.getKey() + ": " + e.getValue());
                }
            }});

        g.addListener(new mapper.graph.Listener<mapper.Map>() {
            public void onEvent(mapper.Map map, mapper.graph.Event event) {
                System.out.println("Graph evnt map");
                System.out.print("graph record "+event+" for map ");
                for (mapper.Signal s : map.signals(Location.SOURCE))
                    System.out.print(s.device().properties().get("name")+":"
                                     +s.properties().get("name")+" ");
                System.out.println("-> ");
                for (mapper.Signal s : map.signals(Location.DESTINATION))
                    System.out.print(s.device().properties().get("name")+":"
                                     +s.properties().get("name")+" ");
                Map.Properties p = map.properties();
                int numProps = p.size();
                for (int i = 0; i < numProps; i++) {
                    Map.Properties.Entry e = p.getEntry(i);
                    System.out.println("  " + e.getKey() + ": " + e.getValue());
                }
            }});

        Signal inp1 = dev1.addSignal(Direction.IN, "insig1", 1, Type.INT32, "Hz",
                                     null, null, null, new mapper.signal.Listener() {
            public void onEvent(Signal sig, mapper.signal.Event e, int v, Time time) {
                System.out.println("in onEvent() for "
                                   +sig.properties().get("name")+": "
                                   +v+" at t="+time.toString());
            }});

        System.out.println("Input signal name: "+inp1.properties().get("name"));

        Signal out1 = dev2.addSignal(Direction.OUT, "outsig1", 1, Type.INT32,
                                     "Hz", 0, 1, null, null);
        Signal out2 = dev2.addSignal(Direction.OUT, "outsig2", 2, Type.FLOAT,
                                     "Hz", 0.0f, 1.0f, null, null);

        dev1.properties().put("width", 256);
        dev1.properties().put("height", 12.5);
        dev1.properties().put("depth", "67");
        dev1.properties().put("deletethis", "should not see me");
        dev1.properties().remove("deletethis");

        Device.Properties p = dev1.properties();
        int numProps = p.size();
        System.out.println("Listing " + numProps + " Device Properties:");
        for (int i = 0; i < numProps; i++) {
            Device.Properties.Entry e = p.getEntry(i);
            System.out.println("  " + e.getKey() + ": " + e.getValue());
        }

        out1.properties().put("width", new int[] {10, 11, 12});
        out1.properties().put("height", 6.25);
        out1.properties().put("depth", new String[]{"one","two"});
        out1.properties().put("deletethis", "or me");
        out1.properties().remove("deletethis");
        out1.properties().put("minimum", 12);

        System.out.println("Signal properties:");
        System.out.println("  name:" + out1.properties().get("name"));

        System.out.println("  height: " + out1.properties().get("height"));
        System.out.println("  width: " + Arrays.toString((int[])out1.properties().get("width")));
        System.out.println("  depth: " + out1.properties().get("depth"));
        System.out.println("  deletethis: " + out1.properties().get("deletethis")
                           + " (should be null)");
        System.out.println("  minimum: " + out1.properties().get("minimum"));
        System.out.println("  maximum: " + out1.properties().get("maximum"));

        System.out.println("Waiting for ready...");
        while (!dev1.ready() || !dev2.ready()) {
            dev1.poll(50);
            dev2.poll(50);
        }
        System.out.println("Devices are ready.");

        System.out.println("  "+dev1.properties().get("name"));
        System.out.println("  "+dev1.properties().get("port"));
        System.out.println("  "+dev1.properties().get("ordinal"));
        System.out.println("  interface="+dev1.graph().getInterface());

        mapper.Map map = new mapper.Map(out1, inp1);
        map.properties().put(Property.EXPRESSION, "y=x*100");
        map.push();

        System.out.print("Establishing map... ");
        while (!map.ready()) {
            dev1.poll(50);
            dev2.poll(50);
        }
        System.out.println("OK");

        int i = 0;

        // Signal should report no value before the first update.
        if (out1.hasValue())
            System.out.println("Signal has value: " + out1.getValue());
        else
            System.out.println("Signal has no value.");

        // Just to test vector-valued signal
        out1.setValue(new int []{i});

        while (i <= 100) {
            System.out.println("Updated signal out1 value to: " + i);
            out1.setValue(i);

            if (i == 50) {
                map.properties().put(Property.EXPRESSION, "y=x*-100");
                map.push();
            }
            dev1.poll(50);
            dev2.poll(50);
            g.poll();
            i++;
        }

        // check graph records
        System.out.println("Graph records:");

        Iterator<Device> devs = g.devices().iterator();
        while (devs.hasNext()) {
            System.out.println("  device: " + devs.next().properties().get("name"));
        }

        // another iterator style
        List<Signal> ins = g.signals();
        for (Signal s : ins) {
            System.out.println("  signal: " + s.properties().get("name"));
        }

        List<mapper.Map> maps = g.maps();
        for (mapper.Map m : maps) {
            System.out.print("  map: ");
            for (mapper.Signal s : m.signals(Location.SOURCE))
                System.out.print(s.device().properties().get("name")+":"
                                 +s.properties().get("name")+" ");
            System.out.print("-> ");
            for (mapper.Signal s : m.signals(Location.DESTINATION))
                System.out.print(s.device().properties().get("name")+":"
                                 +s.properties().get("name")+" ");
            System.out.println();
        }

        System.out.println();
        System.out.println("Number of maps from "
                           + out1.properties().get("name") + ": " + out1.maps().size());
    }
}
