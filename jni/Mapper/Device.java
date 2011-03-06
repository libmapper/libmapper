
package Mapper;

public class Device
{
    public Device() {
        t(12, "twelve");
    }

    native double t(int i, String s); 

    static { 
        System.loadLibrary("mapperjni-0");
    } 
}
