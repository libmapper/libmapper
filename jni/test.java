
import mapper.*;
import mapper.db.*;
import mapper.signal.*;
import mapper.map.*;
import java.util.Arrays;
import java.util.Iterator;

class test {
    public static void main(String [] args) {
        final Device dev = new Device("javatest");
        final Db db = new Db(SubscriptionType.ALL);

        // This is how to ensure the device is freed when the program
        // exits, even on SIGINT.  The Device must be declared "final".
        Runtime.getRuntime().addShutdownHook(new Thread() {
                @Override
                public void run() {
                        dev.free();
                        db.free();
                    }
            });

        db.addDeviceListener(new DeviceListener() {
            public void onEvent(Device dev, mapper.db.Event event) {
                System.out.println("db onEvent() for device "+dev.name());
            }});

        db.addSignalListener(new SignalListener() {
            public void onEvent(Signal sig, mapper.db.Event event) {
                System.out.println("db onEvent() for signal "
                                   +sig.device().name()+":"+sig.name());
            }});

        db.addMapListener(new MapListener() {
            public void onEvent(Map map, mapper.db.Event event) {
                System.out.print("db onEvent() for map ");
                for (int i = 0; i < map.numSources(); i++) {
                    Map.Slot slot = map.sources[i];
                    System.out.print(slot.signal().device().name()
                                     +":"+slot.signal().name()+" ");
                }
                Map.Slot slot = map.destination;
                System.out.println("-> "+slot.signal().device().name()+":"
                                   +slot.signal().name()
                                   +" @expr "+map.expression());
            }});

        Signal inp1 = dev.addInput("insig1", 1, 'f', "Hz", new Value('f', 2.0),
                                   null, new UpdateListener() {
            public void onUpdate(Signal sig, int instanceId, float[] v,
                                 TimeTag tt) {
                System.out.println(" >> in onUpdate() for "+sig.name()+": "
                                   +Arrays.toString(v));
            }});

        System.out.println("Input signal name: "+inp1.name());

        Signal out1 = dev.addOutput("outsig1", 1, 'i', "Hz", new Value('i', 0.0),
                                    new Value('i', 1.0));
        Signal out2 = dev.addOutput("outsig2", 1, 'f', "Hz", new Value(0.0f),
                                    new Value(1.0f));

        dev.setProperty("width", new Value(256));
        dev.setProperty("height", new Value(12.5));
        dev.setProperty("depth", new Value("67"));
        dev.setProperty("deletethis", new Value("should not see me"));
        dev.removeProperty("deletethis");

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
        while (!dev.ready()) {
            dev.poll(100);
        }
        System.out.println("Device is ready.");

        System.out.println("Device name: "+dev.name());
        System.out.println("Device port: "+dev.port());
        System.out.println("Device ordinal: "+dev.ordinal());
        System.out.println("Device interface: "+dev.network().iface());

        Map map = new Map(out1, inp1);
        map.setMode(Mode.EXPRESSION);
        map.setExpression("y=x*100");
        map.source().setMinimum(new Value(15));
        map.source().setMaximum(new Value(-15));
        map.destination().setMaximum(new Value(1000));
        map.destination().setMinimum(new Value(-2000));
        map.sync();

        while (!map.isActive()) { dev.poll(100); }

        int i = 0;
        TimeTag tt = new TimeTag(0,0);

        // Signal should report no value before the first update.
        Value v = out1.value(tt);
        if (!v.isEmpty())
            System.out.println("Signal has value: " + v);
        else
            System.out.println("Signal has no value.");

        // Just to test vector-valued signal and timetag support,
        out1.update(new int []{i}, TimeTag.NOW);

        // Test instances
        out1.setInstanceEventListener(new InstanceEventListener() {
                public void onEvent(Signal sig, int instanceId, int event,
                                    TimeTag tt)
                    {
                        System.out.println("Instance " + instanceId
                                           + " event " + event);
                    }
            }, mapper.signal.InstanceEvent.ALL);

        System.out.println(inp1.name() + " allocation mode: "
                           + inp1.instanceStealingMode());
        inp1.setInstanceStealingMode(StealingMode.NEWEST);
        System.out.println(inp1.name() + " allocation mode: "
                           + inp1.instanceStealingMode());

        out1.reserveInstances(4);
        Signal.Instance instance1 = out1.instance();
        instance1.update(new int[]{-8});
        v = instance1.value();

        Signal.Instance instance2 = out2.instance();
        instance2.update(new float[]{21.9f});
        instance2.value();
        instance2.update(new double[]{48.12});
        instance2.value();
        instance2.release();

        instance1.release();

        inp1.reserveInstances(2, new InstanceUpdateListener() {
            public void onUpdate(Signal.Instance inst, float[] v, TimeTag tt) {
                System.out.println("in onInstanceUpdate() for "
                                   +inst.signal().name()+" instance "
                                   +inst.id()+": "
                                   +Arrays.toString(v));
            }});

        Signal.Instance instance3 = inp1.instance();
        System.out.println(inp1.name() + " instance listener is "
                           + instance3.updateListener());
        instance3.setUpdateListener(new InstanceUpdateListener() {
                public void onUpdate(Signal.Instance inst, float[] v, TimeTag tt) {
                    System.out.println("in alternate-onInstanceUpdate() for "
                                       +inst.signal().name()+" instance "
                                       +inst.id()+": "
                                       +Arrays.toString(v));
                }});
        System.out.println(inp1.name() + " instance listener is "
                           + instance3.updateListener());
        instance3.setUpdateListener(null);
        System.out.println(inp1.name() + " instance listener is "
                           + instance3.updateListener());

        while (i <= 100) {
            System.out.print("Updated value to: " + i);
            out1.update(i);

            v = out1.value(tt);
            if (!v.isEmpty())
                System.out.print("  Signal has value: " + v);
            else
                System.out.print("  Signal has no value.");
            if (i == 50) {
                map.setExpression("y=x*-100");
                map.sync();
            }
            dev.poll(50);
            db.update();
            i++;
        }

        // check db records
        System.out.println("Db records:");

        Iterator<Device> devs = db.devices().iterator();
        while (devs.hasNext()) {
            System.out.println("  device: " + devs.next().name());
        }

        // another iterator style
        mapper.signal.Query ins = db.inputs();
        for (Signal s : ins) {
            System.out.println("  signal: " + s.name());
        }

        mapper.map.Query maps = db.maps();
        for (Map m : maps) {
            System.out.print("  mapping: ");
            for (i = 0; i < m.numSources(); i++)
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

        dev.free();
    }
}
