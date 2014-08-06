
import Mapper.*;
import Mapper.Device.*;
import java.util.Arrays;

class testquery {
    public static void main(String [] args) {
        final Device dev = new Device("javatestquery");
        final Monitor mon = new Monitor();

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

        Mapper.Device.Signal inp1 = dev.addInput("insig1", 1, 'f', "Hz", null,
                                                 null, new InputListener() {
            public void onInput(Mapper.Device.Signal sig,
                                int instanceId,
                                float[] v,
                                TimeTag tt) {
                    System.out.println("in onInput(): "+Arrays.toString(v));
                }});

        Signal out1 = dev.addOutput("outsig1", 1, 'i', "Hz", null, null);
        out1.setCallback(
            new InputListener() {
            public void onInput(Mapper.Device.Signal sig,
                                int instanceId,
                                int[] v,
                                TimeTag tt) {
                    System.out.println("  >> in onQueryResponse(): "+Arrays.toString(v));
                }});

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

        mon.connect(dev.name()+out1.name(), dev.name()+inp1.name(), null);
        while ((dev.numConnectionsIn()) <= 0) { dev.poll(100); }

        int i = 100;
        while (i >= 0) {
            inp1.update(new float[] {i});
            System.out.println("\nUpdating input value to [" + i + "]");
            System.out.println("  Querying...");
            out1.queryRemotes();
            dev.poll(100);
            --i;
        }
        dev.free();
    }
}
