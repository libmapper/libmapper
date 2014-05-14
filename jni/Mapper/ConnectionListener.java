
package Mapper;

public class ConnectionListener {
    /*! The set of possible actions on a local device link or connection. */
    public static final int MDEV_LOCAL_ESTABLISHED = 0;
    public static final int MDEV_LOCAL_MODIFIED    = 1;
    public static final int MDEV_LOCAL_DESTROYED   = 2;

    public void onConnection(Mapper.Device dev,
                             Mapper.Db.Link link,
                             Mapper.Device.Signal sig,
                             Mapper.Db.Connection connection,
                             int action) {};
}
