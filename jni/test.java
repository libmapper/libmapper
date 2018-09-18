
import mpr.*;
import mpr.graph.*;
import mpr.signal.*;
import mpr.map.*;
import java.util.Arrays;
import java.util.Iterator;
import java.util.Map;

class test {
    public static void main(String [] args) {
        final Device dev1 = new Device("javatest");
        final Device dev2 = new Device("javatest");
        final Graph g = new Graph(ObjectType.ALL);
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

        g.addListener(new Listener<Device>() {
            public void onEvent(Device dev, mpr.graph.Event event) {
                System.out.println("graph record "+event+" for device "+dev.getProperty("name"));
                for (int i = 0; i < dev.numProperties(); i++) {
                    Map.Entry e = dev.getProperty(i);
                    System.out.println("  " + e.getKey() + ": " + e.getValue());
                }
            }});

        g.addListener(new Listener<Signal>() {
            public void onEvent(Signal sig, mpr.graph.Event event) {
                System.out.println("graph record "+event+" for signal "
                                   +sig.device().getProperty("name")
                                   +":"+sig.getProperty("name"));
                for (int i = 0; i < sig.numProperties(); i++) {
                    Map.Entry e = sig.getProperty(i);
                    System.out.println("  " + e.getKey() + ": " + e.getValue());
                }
            }});

        g.addListener(new Listener<mpr.Map>() {
            public void onEvent(mpr.Map map, mpr.graph.Event event) {
                System.out.print("graph record "+event+" for map ");
                for (int i = 0; i < map.numSignals(Location.SOURCE); i++) {
                    mpr.Signal signal = map.sources[i];
                    System.out.print(signal.device().getProperty("name")
                                     +":"+signal.getProperty("name")+" ");
                }
                mpr.Signal signal = map.destination;
                System.out.println("-> "+signal.device().getProperty("name")+":"
                                   +signal.getProperty("name"));
                for (int i = 0; i < map.numProperties(); i++) {
                    Map.Entry e = map.getProperty(i);
                    System.out.println("  " + e.getKey() + ": " + e.getValue());
                }
            }});

        Signal inp1 = dev1.addSignal(Direction.INCOMING, 1, "insig1", 1, Type.INT,
                                     "Hz", 2.0f, null, new UpdateListener() {
            public void onUpdate(Signal sig, float[] v, Time time) {
                System.out.println(" >> in onUpdate() for "+sig.getProperty("name")+": "
                                   +Arrays.toString(v)+" at t="+time);
            }});

        System.out.println("Input signal name: "+inp1.getProperty("name"));

        Signal out1 = dev2.addSignal(Direction.OUTGOING, 1, "outsig1", 1, Type.INT,
                                     "Hz", 0, 1, null);
        Signal out2 = dev2.addSignal(Direction.OUTGOING, 1, "outsig2", 1,
                                     Type.FLOAT, "Hz", 0.0f, 1.0f, null);

        dev1.setProperty("width", 256);
        dev1.setProperty("height", 12.5);
        dev1.setProperty("depth", "67");
        dev1.setProperty("deletethis", "should not see me");
        dev1.removeProperty("deletethis");

        int numProps = dev1.numProperties();
        System.out.println("Listing " + numProps + " Device Properties:");
        for (int i = 0; i < numProps; i++) {
            Map.Entry e = dev1.getProperty(i);
            System.out.println("  " + e.getKey() + ": " + e.getValue());
        }

        out1.setProperty("width", new int[] {10, 11, 12});
        out1.setProperty("height", 6.25);
        out1.setProperty("depth", new String[]{"one","two"});
        out1.setProperty("deletethis", "or me");
        out1.removeProperty("deletethis");
        out1.setProperty("minimum", 12);

        System.out.println("Signal properties:");
        System.out.println("  Name of out1: " + out1.getProperty("name"));

        System.out.println("  Looking up `height': " + out1.getProperty("height"));
        System.out.println("  Looking up `width': " + out1.getProperty("width"));
        System.out.println("  Looking up `depth': " + out1.getProperty("depth"));
        System.out.println("  Looking up `deletethis': "
                           + out1.getProperty("deletethis") + " (should be null)");
        System.out.println("  Looking up minimum: " + out1.getProperty("minimum"));
        System.out.println("  Looking up maximum: " + out1.getProperty("maximum"));

        System.out.println("Waiting for ready...");
        while (!dev1.ready() || !dev2.ready()) {
            dev1.poll(50);
            dev2.poll(50);
        }
        System.out.println("Devices are ready.");

        System.out.println("Device name: "+dev1.getProperty("name"));
        System.out.println("Device port: "+dev1.getProperty("port"));
        System.out.println("Device ordinal: "+dev1.getProperty("ordinal"));
        System.out.println("Device interface: "+dev1.graph().getInterface());

        mpr.Map map = new mpr.Map(out1, inp1);
        map.setProperty("expression", "y=x*100");
        map.source().setProperty("minimum", 15);
        map.source().setProperty("maximum", -15);
        map.destination().setProperty("maximum", 1000);
        map.destination().setProperty("minimum", -2000);
        map.push();

        while (!map.ready()) {
            dev1.poll(50);
            dev2.poll(50);
        }

        int i = 0;

        // Signal should report no value before the first update.
        if (out1.hasValue())
            System.out.println("Signal has value: " + out1.intValue());
        else
            System.out.println("Signal has no value.");

        // Just to test vector-valued signal and time support,
        out1.update(new int []{i}, Time.NOW);

        // Test instances
        out1.setInstanceEventListener(new InstanceEventListener() {
            public void onEvent(Signal.Instance inst, InstanceEvent event,
                                Time time) {
                System.out.println("onInstanceEvent() for "
                                   + inst.signal().getProperty("name") + " instance "
                                   + inst.id() + ": " + event.value());
                java.lang.Object userObject = inst.userReference();
                if (userObject != null) {
                    System.out.println("userObject.class = "+userObject.getClass());
                    if (userObject.getClass().equals(int[].class)) {
                        System.out.println("  got int[] userObject "
                                           +Arrays.toString((int[])userObject));
                    }
                }
            }}, mpr.signal.InstanceEvent.ALL);

        System.out.println(inp1.getProperty("name") + " allocation mode: "
                           + inp1.getStealingMode());
        inp1.setStealingMode(StealingMode.NEWEST);
        System.out.println(inp1.getProperty("name") + " allocation mode: "
                           + inp1.getStealingMode());

        System.out.println("Reserving 4 instances for signal "
                           +out1.getProperty("name"));
        out1.reserveInstances(4);
        int[] foo = new int[]{1,2,3,4};
        Signal.Instance instance1 = out1.instance(foo);
        instance1.update(new int[]{-8});
        int v = instance1.intValue();
        Signal.Instance instance2 = out1.instance();
        instance2.update(new float[]{21.9f});
        instance2.intValue();
        instance2.update(new double[]{48.12});
        instance2.intValue();

        inp1.setInstanceUpdateListener(new InstanceUpdateListener() {
            public void onUpdate(Signal.Instance inst, float[] v, Time time) {
                System.out.println("in onInstanceUpdate() for "
                                   +inst.signal().getProperty("name")+" instance "
                                   +inst.id()+": "+inst.userReference()+", val= "
                                   +Arrays.toString(v));
            }});

        System.out.println("Reserving 4 instances for signal "
                           +inp1.getProperty("name"));
        inp1.reserveInstances(4);
        System.out.println(inp1.getProperty("name") + " instance listener is "
                           + inp1.instanceUpdateListener());

        while (i <= 100) {
            if ((i % 3) > 0) {
                System.out.println("Updated instance1 value to: " + i);
                instance1.update(i);
            }
            else {
                System.out.println("Updated instance2 value to: " + i);
                instance2.update(i);
            }

            if (i == 50) {
                map.setProperty(Property.EXPRESSION, "y=x*-100");
                map.source().setProperty(Property.USE_INSTANCES, true);
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
            System.out.println("  device: " + devs.next().getProperty("name"));
        }

        // another iterator style
        List<Signal> ins = g.signals();
        for (Signal s : ins) {
            System.out.println("  signal: " + s.getProperty("name"));
        }

        List<mpr.Map> maps = g.maps();
        for (mpr.Map m : maps) {
            System.out.print("  map: ");
            for (i = 0; i < m.numSignals(Location.SOURCE); i++)
                System.out.print(m.sources[i].device().getProperty("name")+":"
                                 +m.sources[i].getProperty("name")+" ");
            System.out.println("-> "+m.destination.device().getProperty("name")+":"
                               +m.destination.getProperty("name"));
        }

        System.out.println();
        System.out.println("Number of maps from "
                           + out1.getProperty("name") + ": " + out1.maps().size());

        System.out.println(inp1.getProperty("name") + " oldest instance is "
                           + inp1.oldestActiveInstance());
        System.out.println(inp1.getProperty("name") + " newest instance is "
                           + inp1.newestActiveInstance());
    }
}
