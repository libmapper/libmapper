
import Mapper.*;

class test {
    public static void main(String [] args) {
        Device dev = new Device("test", 9000);
        int i = 100;
        while (i >= 0) {
            dev.poll(100);
            --i;
        }
        dev.free();
    }
}
