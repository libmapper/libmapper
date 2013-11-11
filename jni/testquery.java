
import Mapper.*;
import Mapper.Device.*;
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

        Mapper.Device.Signal inp1 = dev.add_input("insig1", 1, 'f', "Hz", 2.0, null,
            new InputListener() {
            public void onInput(Mapper.Device.Signal sig,
                                Mapper.Db.Signal props,
                                int instance_id,
                                float[] v,
                                TimeTag tt) {
                    System.out.println("in onInput(): "+Arrays.toString(v));
                }});

        Signal out1 = dev.add_output("outsig1", 1, 'i', "Hz", 0.0, 1.0);
        out1.set_callback(
            new InputListener() {
            public void onInput(Mapper.Device.Signal sig,
                                Mapper.Db.Signal props,
                                int instance_id,
                                int[] v,
                                TimeTag tt) {
                    System.out.println("in onQueryResponse(): "+Arrays.toString(v));
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

        int i = 100;
        while (--i >= 0) {
            dev.poll(100);
            inp1.update(new float[] {i});
            System.out.println("  Updated input value: " + i);
            System.out.println("  Querying...");
            out1.query_remotes();
        }
        dev.free();
    }
}
