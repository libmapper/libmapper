
package Mapper;

public class LinkListener {
    /*! The set of possible actions on a local device link or connection. */
    public static final int MDEV_LOCAL_ESTABLISHED = 0;
    public static final int MDEV_LOCAL_MODIFIED    = 1;
    public static final int MDEV_LOCAL_DESTROYED   = 2;

    public void onLink(Mapper.Device dev,
                       Mapper.Db.Link link,
                       int action) {};
}
