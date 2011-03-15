
import Mapper.*;

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

        dev.add_input("insig1", 1, 'f', "Hz", 2.0, null,
                      new InputListener() {
                          public void onInput() {
                              System.out.println("in onInput()");
                          }});

        dev.add_output("outsig1", 1, 'f', "Hz", 0.0, 1.0);

        int i = 100;
        while (i >= 0) {
            dev.poll(100);
            --i;
        }
        dev.free();
    }
}
