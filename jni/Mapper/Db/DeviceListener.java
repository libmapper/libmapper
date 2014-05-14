
package Mapper.Db;

public class DeviceListener {
    /*! The set of possible actions on a local device link or connection. */
    public static final int MDB_MODIFY = 0;
    public static final int MDB_NEW    = 1;
    public static final int MDB_REMOVE = 2;

    public void onDevice(Mapper.Db.Device record,
                         int action) {};
}
