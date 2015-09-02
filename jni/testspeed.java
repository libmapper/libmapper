
import mapper.*;
import mapper.signal.*;
import java.util.Arrays;

class testspeed {
    public static boolean updated = true;

    public static void main(String [] args) {
        final Device dev = new Device("javatest");

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

        UpdateListener l = new UpdateListener() {
            public void onUpdate(Signal sig, int instanceId, float[] v,
                                 TimeTag tt) {
                testspeed.updated = true;
            }
        };

        Signal in = dev.addInput("insig", 1, 'f', "Hz", null, null, l);

        Signal out = dev.addOutput("outsig", 1, 'i', "Hz", null, null);

        System.out.println("Waiting for ready...");
        while (!dev.ready()) {
            dev.poll(100);
        }
        System.out.println("Device is ready.");

        Map map = new Map(out, in);
        while (!map.isActive()) { dev.poll(100); }

        double then = dev.now().getDouble();
        int i = 0;
        while (i < 10000) {
            if (testspeed.updated) {
                out.update(i);
                i++;
                testspeed.updated = false;
            }
            dev.poll(1);
        }
        double elapsed = dev.now().getDouble() - then;
        System.out.println("Sent "+i+" messages in "+elapsed+" seconds.");
        dev.free();
    }
}
