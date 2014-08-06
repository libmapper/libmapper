
package Mapper;

public class InputListener {
    public void onInput(Mapper.Device.Signal sig,
                        int instanceId,
                        float[] v,
                        TimeTag tt) {};
    public void onInput(Mapper.Device.Signal sig,
                        int instanceId,
                        int[] v,
                        TimeTag tt) {};
    public void onInput(Mapper.Device.Signal sig,
                        int instanceId,
                        double[] v,
                        TimeTag tt) {};
}
