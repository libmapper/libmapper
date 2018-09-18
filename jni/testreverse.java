
import mpr.*;
import mpr.signal.*;
import java.util.Arrays;

class testreverse {
    public static void main(String [] args) {
        final Device dev = new Device("javatestreverse");

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

        Signal inp1 = dev.addSignal(Direction.INCOMING, 1, "insig1", 1, Type.FLOAT,
                                    "Hz", null, null, new UpdateListener() {
                public void onUpdate(Signal sig, float[] v, Time time) {
                    System.out.println("in onUpdate(): "+Arrays.toString(v));
                }});

        Signal out1 = dev.addSignal(Direction.OUTGOING, 1, "outsig1", 1, Type.INT,
                                    "Hz", null, null, null);
        out1.setUpdateListener(
            new UpdateListener() {
                public void onUpdate(Signal sig, int[] v, Time time) {
                    System.out.println("  >> in onUpdate(): "+Arrays.toString(v));
                }});

        System.out.println("Waiting for ready...");
        while (!dev.ready()) {
            dev.poll(100);
        }
        System.out.println("Device is ready.");

        System.out.println("Device name: "+dev.getProperty(Property.NAME));
        System.out.println("Device port: "+dev.getProperty(Property.PORT));
        System.out.println("Device ordinal: "+dev.getProperty(Property.ORDINAL));
        System.out.println("Network interface: "+dev.graph().getInterface());

        Map map = new Map(inp1, out1);
        map.push();
        while (!map.ready()) {
            System.out.println("waiting for map");
            dev.poll(100);
        }

        int i = 100;
        while (i >= 0) {
            System.out.println("Updating input to ["+i+"]");
            inp1.update(new int[] {i});
            dev.poll(100);
            --i;
        }
        dev.free();
    }
}
