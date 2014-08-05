
import Mapper.*;
import Mapper.Device.*;
import java.util.Arrays;
import java.util.Iterator;

class test {
    public static void main(String [] args) {
        final Device dev = new Device("javatest");
        final Monitor mon = new Monitor(Mapper.Monitor.SUB_DEVICE_ALL);

        // This is how to ensure the device is freed when the program
        // exits, even on SIGINT.  The Device must be declared "final".
        Runtime.getRuntime().addShutdownHook(new Thread()
            {
                @Override
                public void run()
                    {
                        dev.free();
                        mon.free();
                    }
            });

        mon.Db.addDeviceCallback(new Mapper.Db.DeviceListener() {
            public void onEvent(Mapper.Db.Device d, int event) {
                System.out.println("db onEvent() for device "+d.name());
            }});

        mon.Db.addSignalCallback(new Mapper.Db.SignalListener() {
            public void onEvent(Mapper.Db.Signal s, int event) {
                System.out.println("db onEvent() for signal "+s.name());
            }});

        mon.Db.addLinkCallback(new Mapper.Db.LinkListener() {
            public void onEvent(Mapper.Db.Link l, int event) {
                System.out.println("db onEvent() for link "
                                   +l.srcName()+" -> "+l.destName());
            }});

        mon.Db.addConnectionCallback(new Mapper.Db.ConnectionListener() {
            public void onEvent(Mapper.Db.Connection c, int event) {
                System.out.println("db onEvent() for connection "
                                   +c.srcName+" -> "+c.destName+" @expr "
                                   +c.expression);
            }});

        Mapper.Device.Signal inp1 = dev.addInput("insig1", 1, 'f', "Hz",
                                                 new PropertyValue('f', 2.0),
                                                 null, new InputListener() {
            public void onInput(Mapper.Device.Signal sig,
                                int instanceId,
                                float[] v,
                                TimeTag tt) {
                System.out.println(" >> in onInput() for "+sig.name()+": "
                                   +Arrays.toString(v));
            }});

        System.out.println("Input signal name: "+inp1.name());

        Signal out1 = dev.addOutput("outsig1", 1, 'i', "Hz",
                                    new PropertyValue('i', 0.0),
                                    new PropertyValue('i', 1.0));
        Signal out2 = dev.addOutput("outsig2", 1, 'f', "Hz",
                                    new PropertyValue(0.0f),
                                    new PropertyValue(1.0f));

        System.out.println("Output signal index: "+out1.index());
        System.out.println("Zeroeth output signal name: "+dev.getOutput(0).name());

        dev.setProperty("width", new PropertyValue(256));
        dev.setProperty("height", new PropertyValue(12.5));
        dev.setProperty("depth", new PropertyValue("67"));
        dev.setProperty("deletethis", new PropertyValue("should not see me"));
        dev.removeProperty("deletethis");

        out1.setProperty("width", new PropertyValue(new int[] {10, 11, 12}));
        out1.setProperty("height", new PropertyValue(6.25));
        out1.setProperty("depth", new PropertyValue(new String[]{"one","two"}));
        out1.setProperty("deletethis", new PropertyValue("or me"));
        out1.removeProperty("deletethis");
        out1.setMinimum(new PropertyValue(12));

        System.out.println("Signal properties:");
        System.out.println("  Name of out1: " + out1.properties().name());

        System.out.println("  Looking up `height': "
                           + out1.properties().property("height"));
        System.out.println("  Looking up `width': "
                           + out1.properties().property("width"));
        System.out.println("  Looking up `depth': "
                           + out1.properties().property("depth"));
        System.out.println("  Looking up `deletethis': "
                           + out1.properties().property("deletethis")
                           + " (should be null)");
        System.out.println("  Looking up minimum: "
                           + out1.properties().minimum());
        System.out.println("  Looking up maximum: "
                           + out1.properties().maximum());

        System.out.println("Waiting for ready...");
        while (!dev.ready()) {
            dev.poll(100);
        }
        System.out.println("Device is ready.");

        System.out.println("Device name: "+dev.name());
        System.out.println("Device port: "+dev.port());
        System.out.println("Device ordinal: "+dev.ordinal());
        System.out.println("Device interface: "+dev.iface());
        System.out.println("Device ip4: "+dev.ip4());

        mon.link(dev.name(), dev.name(), null);
        while (dev.numLinksIn() <= 0) { dev.poll(100); }

        Mapper.Db.Connection c = new Mapper.Db.Connection();
        c.mode = Mapper.Db.Connection.MO_EXPRESSION;
        c.expression = "y=x*100";
        c.srcMin = new PropertyValue(15);
        c.srcMax = new PropertyValue(-15);
        c.destMax = new PropertyValue(1000);
        c.destMin = new PropertyValue(-2000);
        mon.connect(dev.name()+out1.name(), dev.name()+inp1.name(), c);
        while ((dev.numConnectionsIn()) <= 0) { dev.poll(100); }

        int i = 0;
        double [] ar = new double [] {0};
        TimeTag tt = new TimeTag(0,0);

        // Signal should report no value before the first update.
        if (out1.value(ar, tt))
            System.out.println("Signal has value: " + ar[0]);
        else
            System.out.println("Signal has no value.");

        // Just to test vector-valued signal and timetag support,
        out1.update(new int []{i}, TimeTag.NOW);

        // Test instances
        out1.setInstanceEventCallback(new InstanceEventListener() {
                public void onEvent(Mapper.Device.Signal sig,
                                    int instanceId,
                                    int event,
                                    TimeTag tt)
                    {
                        System.out.println("Instance "
                                           + instanceId
                                           + " event " + event);
                    }
            }, InstanceEventListener.IN_ALL);

        System.out.println(inp1.name() + " allocation mode: "
                           + inp1.instanceAllocationMode());
        inp1.setInstanceAllocationMode(Device.Signal.IN_STEAL_NEWEST);
        System.out.println(inp1.name() + " allocation mode: "
                           + inp1.instanceAllocationMode());

        out1.reserveInstances(new int[]{10, 11, 12});
        out1.updateInstance(10, new int[]{-8});
        out1.instanceValue(10, new int[]{0});
        out1.releaseInstance(10);

        out2.reserveInstances(3);
        out2.updateInstance(1, new float[]{21.9f});
        out2.instanceValue(1, new float[]{0});
        out2.updateInstance(1, new double[]{48.12});
        out2.instanceValue(1, new double[]{0});
        out2.releaseInstance(1);

        inp1.reserveInstances(3, new InputListener() {
                public void onInput(Mapper.Device.Signal sig,
                                    int instanceId,
                                    float[] v,
                                    TimeTag tt) {
                    System.out.println("in onInput() for "
                                       +sig.name()+" instance "
                                       +instanceId+": "
                                       +Arrays.toString(v));
                }});
        System.out.println(inp1.name() + " instance 1 cb is "
                           + inp1.getInstanceCallback(1));
        inp1.setInstanceCallback(1, new InputListener() {
                public void onInput(Mapper.Device.Signal sig,
                                    int instanceId,
                                    float[] v,
                                    TimeTag tt) {
                    System.out.println("in onInput() for "
                                       +sig.name()+" instance 1: "
                                       +Arrays.toString(v));
                }});
        System.out.println(inp1.name() + " instance 1 cb is "
                           + inp1.getInstanceCallback(1));
        inp1.setInstanceCallback(1, null);
        System.out.println(inp1.name() + " instance 1 cb is "
                           + inp1.getInstanceCallback(1));

        while (i <= 100) {
            System.out.print("Updated value to: " + i);
            out1.update(i);

            // Note, we are testing an implicit cast from int to float
            // here because we are passing a double[] into
            // out1.value().
            if (out1.value(ar, tt))
                System.out.print("  Signal has value: " + ar[0]);
            else
                System.out.print("  Signal has no value.");

            if (i == 50) {
                Mapper.Db.Connection mod = new Mapper.Db.Connection();
                mod.expression = "y=x*-100";
                System.out.println("Should be connecting "+dev.name()+out1.name()+" -> "+dev.name()+inp1.name());
                mon.modifyConnection(dev.name()+out1.name(),
                                     dev.name()+inp1.name(),
                                     mod);
            }

            dev.poll(50);
            mon.poll(50);
            i++;
        }

        // check monitor.db records
        System.out.println("Db records:");

        Iterator<Mapper.Db.Device> devs = mon.Db.devices().iterator();
        while (devs.hasNext()) {
            System.out.println("  device: " + devs.next().name());
        }

        // another iterator style
        Mapper.Db.SignalCollection ins = mon.Db.inputs();
        for (Mapper.Db.Signal s : ins) {
            System.out.println("  signal: " + s.name());
        }

        Mapper.Db.LinkCollection links = mon.Db.links();
        for (Mapper.Db.Link l : links) {
            System.out.println("  link: "+ l.srcName() + " -> " + l.destName());
        }

        Mapper.Db.ConnectionCollection cons = mon.Db.connections();
        for (Mapper.Db.Connection cc : cons) {
            System.out.println("  connection: "+ cc.srcName + " -> " + cc.destName);
        }

        System.out.println();
        System.out.println("Number of connections from "
                           + out1.name() + ": " + out1.numConnections());

        System.out.println(inp1.name() + " oldest instance is "
                           + inp1.oldestActiveInstance());
        System.out.println(inp1.name() + " newest instance is "
                           + inp1.newestActiveInstance());

        dev.free();
    }
}
