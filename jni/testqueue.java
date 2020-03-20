
import mpr.*;
import mpr.signal.*;
import java.util.Arrays;

class testqueue {
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

        Listener l = new Listener() {
            public void onEvent(Signal sig, mpr.signal.Event e, float v, Time time) {
                testqueue.updated = true;
            }
        };

        Signal in = dev.addSignal(Direction.INCOMING, "insig", 1, Type.FLOAT,
                                  "Hz", null, null, null, l);

        Signal out = dev.addSignal(Direction.OUTGOING, "outsig", 1, Type.INT32,
                                   "Hz", null, null, null, null);

        System.out.println("Waiting for ready...");
        while (!dev.ready()) {
            dev.poll(100);
        }
        System.out.println("Device is ready.");

        Map map = new Map(out, in);
        map.push();
        while (!map.ready()) {
            dev.poll(100);
        }

        Time time = new Time();
        Time qtime;
        double then = time.getDouble();
        int i = 0;
        while (i < 10000) {
            if (testqueue.updated) {
                qtime = dev.startQueue();
                out.setValue(i);
                dev.sendQueue(qtime);
                i++;
                testqueue.updated = false;
                if ((i % 1000) == 0)
                    System.out.print(".");
            }
            dev.poll(1);
        }
        double elapsed = time.now().getDouble() - then;
        System.out.println("Sent "+i+" messages in "+elapsed+" seconds.");
        dev.free();
    }
}
