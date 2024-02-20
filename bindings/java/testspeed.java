
import mapper.*;
import mapper.signal.*;
import java.util.Arrays;

class testspeed {
    public static boolean updated = true;

    private static int i = 0;
    public static int incCounter() {
        i++;
        return i;
    }

    public static void main(String [] args) {
        final Device dev1 = new Device("java.testspeed.send");
        final Device dev2 = new Device("java.testspeed.recv");

        // This is how to ensure the device is freed when the program
        // exits, even on SIGINT.  The Device must be declared "final".
        Runtime.getRuntime().addShutdownHook(new Thread()
            {
                @Override
                public void run()
                    {
                        dev1.free();
                        dev2.free();
                    }
            });

        Listener l = new Listener() {
            public void onEvent(Signal sig, mapper.signal.Event e, float v, Time time) {
                if (e == mapper.signal.Event.UPDATE)
                    testspeed.updated = true;
            }
        };

        Signal out = dev1.addSignal(mapper.signal.Direction.OUTGOING, "outsig", 1, Type.INT32,
                                    "Hz", null, null, null, null);

        Signal in = dev2.addSignal(mapper.signal.Direction.INCOMING, "insig", 1, Type.FLOAT,
                                   "Hz", null, null, null, new Listener() {
            public void onEvent(Signal sig, mapper.signal.Event e, float v, Time time) {
                if (e == mapper.signal.Event.UPDATE) {
                    out.setValue(new int[] {incCounter()});
                }
            }
        });

        System.out.println("Waiting for ready...");
        while (!dev1.ready() || !dev2.ready()) {
            dev1.poll(100);
            dev2.poll(100);
        }
        System.out.println("Devices are ready.");

        Map map = new Map(out, in);
        map.push();
        System.out.println("Waiting for map");
        while (!map.ready()) {
            dev1.poll(100);
            dev2.poll(100);
        }
        System.out.println("Map is ready.");

        Time time = new Time();
        double then = time.getDouble();
        out.setValue(new int[] {i});
        while (i < 10000) {
            if ((i % 1000) == 0)
                System.out.print('.');
            dev1.poll();
            dev2.poll();
        }
        double elapsed = time.now().getDouble() - then;
        System.out.println("Sent "+i+" messages in "+elapsed+" seconds.");
        dev1.free();
        dev2.free();
    }
}
