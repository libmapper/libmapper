
import mapper.*;
import mapper.signal.*;
import java.util.Arrays;

class testreverse {
    public static void main(String [] args) {
        final Device dev = new Device("java.testreverse");

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

        Signal input = dev.addSignal(Direction.IN, "insig", 1, Type.FLOAT,
                                     "Hz", null, null, null, new Listener() {
            public void onEvent(Signal sig, mapper.signal.Event e, float v, Time time) {
                if (e == mapper.signal.Event.UPDATE)
                    System.out.println("  insig got: "+v);
        }});

        Signal output = dev.addSignal(Direction.OUT, "outsig", 1, Type.INT32,
                                      "Hz", null, null, null, new Listener() {
            public void onEvent(Signal sig, mapper.signal.Event e, int v, Time time) {
                if (e == mapper.signal.Event.UPDATE)
                    System.out.println("  outsig got(): "+v);
        }});

        System.out.println("Waiting for ready...");
        while (!dev.ready()) {
            dev.poll(100);
        }
        System.out.println("Device is ready.");

        System.out.println("Device name: "+dev.properties().get(Property.NAME));
        System.out.println("Device port: "+dev.properties().get(Property.PORT));
        System.out.println("Device ordinal: "+dev.properties().get(Property.ORDINAL));
        System.out.println("Network interface: "+dev.graph().getInterface());

        Map map = new Map(input, output);
        map.push();
        while (!map.ready()) {
            System.out.println("waiting for map");
            dev.poll(100);
        }

        int i = 100;
        while (i >= 0) {
            System.out.println("Updating input to ["+i+"]");
            input.setValue(new int[] {i});
            dev.poll(100);
            --i;

            if (i == 50) {
                System.out.println("Setting outsig direction to INPUT");
                output.properties().put(Property.DIRECTION, Direction.IN);
                output.push();
            }
        }
        dev.free();
    }
}
