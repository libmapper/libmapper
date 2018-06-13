
import mapper.*;
import mapper.signal.*;
import java.util.Arrays;

class testquery {
    public static void main(String [] args) {
        final Device dev = new Device("javatestquery");

        // This is how to ensure the device is freed when the program
        // exits, even on SIGINT.  The Device must be declared "final".
        Runtime.getRuntime().addShutdownHook(new Thread()
            {
                @Override
                public void run()
                    {
                        dev.free();
                    }
            });

        Signal inp1 = dev.addInputSignal("insig1", 1, Type.FLOAT, "Hz", null, null,
                                         new UpdateListener() {
            public void onUpdate(Signal sig, float[] v, TimeTag tt) {
                    System.out.println("in onUpdate(): "+Arrays.toString(v));
                }});

        Signal out1 = dev.addOutputSignal("outsig1", 1, Type.INT, "Hz", null, null);
        out1.setUpdateListener(
            new UpdateListener() {
            public void onUpdate(Signal sig, int[] v, TimeTag tt) {
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
        System.out.println("Device interface: "+dev.network().iface());

        Map map = new Map(out1, inp1).push();
        while (!map.ready()) { dev.poll(100); }

        int i = 100;
        while (i >= 0) {
            inp1.update(new float[] {i});
            System.out.println("Updating input value to [" + i + "]");
            System.out.println("  Querying...");
            out1.queryRemotes();
            dev.poll(100);
            --i;
        }
        dev.free();
    }
}
