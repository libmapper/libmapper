
package Mapper;

public class InputListener {
    public void onInput(Mapper.Device.Signal sig,
                        Mapper.Db.Signal props,
                        int instance_id,
                        float[] v,
                        TimeTag tt) {};
    public void onInput(Mapper.Device.Signal sig,
                        Mapper.Db.Signal props,
                        int instance_id,
                        int[] v,
                        TimeTag tt) {};
}
