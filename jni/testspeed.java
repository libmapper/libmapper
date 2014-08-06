
import Mapper.*;
import Mapper.Device.*;
import java.util.Arrays;

class testspeed {
    public static boolean updated = true;

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

        InputListener h = new InputListener() {
            public void onInput(Mapper.Device.Signal sig,
                                int instanceId,
                                float[] v,
                                TimeTag tt) {
                testspeed.updated = true;
            }
        };

        Mapper.Device.Signal in = dev.addInput("insig", 1, 'f', "Hz", null,
                                               null, h);

        Signal out = dev.addOutput("outsig", 1, 'i', "Hz", null, null);

        System.out.println("Waiting for ready...");
        while (!dev.ready()) {
            dev.poll(100);
        }
        System.out.println("Device is ready.");

        mon.link(dev.name(), dev.name(), null);
        while (dev.numLinksIn() <= 0) { dev.poll(100); }

        Mapper.Db.Connection c = new Mapper.Db.Connection();
        mon.connect(dev.name()+out.name(), dev.name()+in.name(), null);
        while ((dev.numConnectionsIn()) <= 0) { dev.poll(100); }

        mon.free();

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
