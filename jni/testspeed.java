
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
            public void onUpdate(Signal sig, float[] v, TimeTag tt) {
                testspeed.updated = true;
            }
        };

        Signal in = dev.addInputSignal("insig", 1, 'f', "Hz", null, null, l);

        Signal out = dev.addOutputSignal("outsig", 1, 'i', "Hz", null, null);

        System.out.println("Waiting for ready...");
        while (!dev.ready()) {
            dev.poll(100);
        }
        System.out.println("Device is ready.");

        Map map = new Map(out, in).push();
        while (!map.ready()) {
            dev.poll(100);
        }

        TimeTag tt = new TimeTag();
        double then = tt.getDouble();
        int i = 0;
        while (i < 10000) {
            if (testspeed.updated) {
                out.update(i);
                i++;
                testspeed.updated = false;
                if ((i % 1000) == 0)
                    System.out.print(".");
            }
            dev.poll(1);
        }
        double elapsed = tt.now().getDouble() - then;
        System.out.println("Sent "+i+" messages in "+elapsed+" seconds.");
        dev.free();
    }
}
