
import mapper.*;
import mapper.database.*;
import mapper.signal.*;
import mapper.map.*;
import java.util.Arrays;
import java.util.Iterator;

class test {
    public static void main(String [] args) {
        final Device dev1 = new Device("javatest");
        final Device dev2 = new Device("javatest");
        final Database db = new Database(ObjectType.ALL);
        TimeTag start = new TimeTag();
        System.out.println("Current time: "+start.now());

        // This is how to ensure the device is freed when the program
        // exits, even on SIGINT.  The Device must be declared "final".
        Runtime.getRuntime().addShutdownHook(new Thread() {
                @Override
                public void run() {
                        dev1.free();
                        dev2.free();
                        db.free();
                    }
            });

        db.addDeviceListener(new DeviceListener() {
            public void onEvent(Device dev, mapper.database.Event event) {
                System.out.println("db record "+event+" for device "+dev.name());
                for (int i = 0; i < dev.numProperties(); i++) {
                    Property p = dev.property(i);
                    System.out.println("  " + p.name + ": " + p.value);
                }
            }});

        db.addLinkListener(new LinkListener() {
            public void onEvent(Link link, mapper.database.Event event) {
                System.out.println("db record "+event+" for link "
                                   +link.device(0).name()+"<->"
                                   +link.device(1).name());
                for (int i = 0; i < link.numProperties(); i++) {
                    Property p = link.property(i);
                    System.out.println("  " + p.name + ": " + p.value);
                }
            }});

        db.addSignalListener(new SignalListener() {
            public void onEvent(Signal sig, mapper.database.Event event) {
                System.out.println("db record "+event+" for signal "
                                   +sig.device().name()+":"+sig.name());
                for (int i = 0; i < sig.numProperties(); i++) {
                    Property p = sig.property(i);
                    System.out.println("  " + p.name + ": " + p.value);
                }
            }});

        db.addMapListener(new MapListener() {
            public void onEvent(Map map, mapper.database.Event event) {
                System.out.print("db record "+event+" for map ");
                for (int i = 0; i < map.numSlots(Location.SOURCE); i++) {
                    Map.Slot slot = map.sources[i];
                    System.out.print(slot.signal().device().name()
                                     +":"+slot.signal().name()+" ");
                }
                Map.Slot slot = map.destination;
                System.out.println("-> "+slot.signal().device().name()+":"
                                   +slot.signal().name());
                for (int i = 0; i < map.numProperties(); i++) {
                    Property p = map.property(i);
                    System.out.println("  " + p.name + ": " + p.value);
                }
            }});

        Signal inp1 = dev1.addInputSignal("insig1", 1, 'f', "Hz",
                                          new Value('f', 2.0), null,
                                          new UpdateListener() {
            public void onUpdate(Signal sig, float[] v, TimeTag tt) {
                System.out.println(" >> in onUpdate() for "+sig.name()+": "
                                   +Arrays.toString(v)+" at t="+tt);
            }});

        System.out.println("Input signal name: "+inp1.name());

        Signal out1 = dev2.addOutputSignal("outsig1", 1, 'i', "Hz",
                                           new Value('i', 0.0),
                                           new Value('i', 1.0));
        Signal out2 = dev2.addOutputSignal("outsig2", 1, 'f', "Hz",
                                           new Value(0.0f), new Value(1.0f));

        dev1.setProperty("width", new Value(256));
        dev1.setProperty("height", new Value(12.5));
        dev1.setProperty("depth", new Value("67"));
        dev1.setProperty("deletethis", new Value("should not see me"));
        dev1.removeProperty("deletethis");

        int numProps = dev1.numProperties();
        System.out.println("Listing " + numProps + " Device Properties:");
        for (int i = 0; i < numProps; i++) {
            Property p = dev1.property(i);
            System.out.println("  " + p.name + ": " + p.value);
        }

        out1.setProperty("width", new Value(new int[] {10, 11, 12}));
        out1.setProperty("height", new Value(6.25));
        out1.setProperty("depth", new Value(new String[]{"one","two"}));
        out1.setProperty("deletethis", new Value("or me"));
        out1.removeProperty("deletethis");
        out1.setMinimum(new Value(12));

        System.out.println("Signal properties:");
        System.out.println("  Name of out1: " + out1.name());

        System.out.println("  Looking up `height': " + out1.property("height"));
        System.out.println("  Looking up `width': " + out1.property("width"));
        System.out.println("  Looking up `depth': " + out1.property("depth"));
        System.out.println("  Looking up `deletethis': "
                           + out1.property("deletethis") + " (should be null)");
        System.out.println("  Looking up minimum: " + out1.minimum());
        System.out.println("  Looking up maximum: " + out1.maximum());

        System.out.println("Waiting for ready...");
        while (!dev1.ready() || !dev2.ready()) {
            dev1.poll(50);
            dev2.poll(50);
        }
        System.out.println("Devices are ready.");

        System.out.println("Device name: "+dev1.name());
        System.out.println("Device port: "+dev1.port());
        System.out.println("Device ordinal: "+dev1.ordinal());
        System.out.println("Device interface: "+dev1.network().iface());

        Map map = new Map(out1, inp1);
        map.setMode(Mode.EXPRESSION);
        map.setExpression("y=x*100");
        map.source().setMinimum(new Value(15));
        map.source().setMaximum(new Value(-15));
        map.destination().setMaximum(new Value(1000));
        map.destination().setMinimum(new Value(-2000));
        map.push();

        while (!map.ready()) {
            dev1.poll(50);
            dev2.poll(50);
        }

        int i = 0;

        // Signal should report no value before the first update.
        Value v = out1.value();
        if (!v.isEmpty())
            System.out.println("Signal has value: " + v);
        else
            System.out.println("Signal has no value.");

        // Just to test vector-valued signal and timetag support,
        out1.update(new int []{i}, TimeTag.NOW);

        // Test instances
        out1.setInstanceEventListener(new InstanceEventListener() {
            public void onEvent(Signal.Instance inst, InstanceEvent event,
                                TimeTag tt) {
                System.out.println("onInstanceEvent() for "
                                   + inst.signal().name() + " instance "
                                   + inst.id() + ": " + event.value());
                Object userObject = inst.userReference();
                if (userObject != null) {
                    System.out.println("userObject.class = "+userObject.getClass());
                    if (userObject.getClass().equals(int[].class)) {
                        System.out.println("  got int[] userObject "
                                           +Arrays.toString((int[])userObject));
                    }
                }
            }}, mapper.signal.InstanceEvent.ALL);

        System.out.println(inp1.name() + " allocation mode: "
                           + inp1.instanceStealingMode());
        inp1.setInstanceStealingMode(StealingMode.NEWEST);
        System.out.println(inp1.name() + " allocation mode: "
                           + inp1.instanceStealingMode());

        System.out.println("Reserving 4 instances for signal "+out1.name());
        out1.reserveInstances(4);
        int[] foo = new int[]{1,2,3,4};
        Signal.Instance instance1 = out1.instance(foo);
        instance1.update(new int[]{-8});
        v = instance1.value();
        Signal.Instance instance2 = out1.instance();
        instance2.update(new float[]{21.9f});
        instance2.value();
        instance2.update(new double[]{48.12});
        instance2.value();

        inp1.setInstanceUpdateListener(new InstanceUpdateListener() {
            public void onUpdate(Signal.Instance inst, float[] v, TimeTag tt) {
                System.out.println("in onInstanceUpdate() for "
                                   +inst.signal().name()+" instance "
                                   +inst.id()+": "+inst.userReference()+", val= "
                                   +Arrays.toString(v));
            }});

        System.out.println("Reserving 4 instances for signal "+inp1.name());
        inp1.reserveInstances(4);
        System.out.println(inp1.name() + " instance listener is "
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
                map.setExpression("y=x*-100");
                map.source().setUseInstances(true);
                map.push();
            }
            dev1.poll(50);
            dev2.poll(50);
            db.poll();
            i++;
        }

        // check database records
        System.out.println("Database records:");

        Iterator<Device> devs = db.devices().iterator();
        while (devs.hasNext()) {
            System.out.println("  device: " + devs.next().name());
        }

        // another iterator style
        mapper.link.Query links = db.links();
        for (Link l : links) {
            System.out.println("  link: " + l.device(0).name()
                               + "<->" + l.device(1).name());
        }
        mapper.signal.Query ins = db.signals();
        for (Signal s : ins) {
            System.out.println("  signal: " + s.name());
        }

        mapper.map.Query maps = db.maps();
        for (Map m : maps) {
            System.out.print("  map: ");
            for (i = 0; i < m.numSlots(Location.SOURCE); i++)
                System.out.print(m.sources[i].signal().device().name()+":"
                                 +m.sources[i].signal().name()+" ");
            System.out.println("-> "+m.destination.signal().device().name()+":"
                               +m.destination.signal().name());
        }

        System.out.println();
        System.out.println("Number of maps from "
                           + out1.name() + ": " + out1.numMaps());

        System.out.println(inp1.name() + " oldest instance is "
                           + inp1.oldestActiveInstance());
        System.out.println(inp1.name() + " newest instance is "
                           + inp1.newestActiveInstance());
    }
}
