
package Mapper;

public class InstanceEventListener {
    /*! The set of possible actions on an instance, used to
     *  register callbacks to inform them of what is happening. */
    public static final int IN_NEW                = 0x01;
    public static final int IN_UPSTREAM_RELEASE   = 0x02;
    public static final int IN_DOWNSTREAM_RELEASE = 0x04;
    public static final int IN_OVERFLOW           = 0x08;
    public static final int IN_ALL                = 0xFF;

    public void onEvent(Mapper.Device.Signal sig,
                        int instanceId,
                        int event,
                        TimeTag tt) {};
}
