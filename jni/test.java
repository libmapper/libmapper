
import Mapper.*;

class test {
    public static void main(String [] args) {
        final Device dev = new Device("test", 9000);

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

        int i = 100;
        while (i >= 0) {
            dev.poll(100);
            --i;
        }
        dev.free();
    }
}
