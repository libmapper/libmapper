
import Mapper.*;
import Mapper.Device.*;

class test {
    public static void main(String [] args) {
        final Device dev = new Device("javatest", 9000);

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
                public void onInput() {
                    System.out.println("in onInput()");
                }});

        System.out.println("Input signal name: "+inp1.name());

        Signal out1 = dev.add_output("outsig1", 1, 'i', "Hz", 0.0, 1.0);
        System.out.println("Output signal index: "+out1.index());
        System.out.println("Zeroeth output signal name: "+dev.get_output_by_index(0).name());

        Signal inp2 = dev.add_hidden_input("insig2", 1, 'f', "Hz", 2.0, null,
            new InputListener() {
                public void onInput() {
                    System.out.println("in onInput() for inp2");
                }});

        out1.query_remote(inp2);

        dev.set_property("width", new PropertyValue(256));
        dev.set_property("height", new PropertyValue(12.5));
        dev.set_property("depth", new PropertyValue("67"));
        dev.set_property("deletethis", new PropertyValue("should not see me"));
        dev.remove_property("deletethis");

        out1.set_property("width", new PropertyValue(128));
        out1.set_property("height", new PropertyValue(6.25));
        out1.set_property("depth", new PropertyValue("test"));
        out1.set_property("deletethis", new PropertyValue("or me"));
        out1.remove_property("deletethis");

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
        while (i >= 0) {
            dev.poll(100);
            --i;
            out1.update(new int[] {i});
        }
        dev.free();
    }
}
