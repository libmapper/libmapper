
package Mapper.Db;

import java.util.Iterator;

public class DeviceIterator implements Iterator<Mapper.Db.Device> {
    public DeviceIterator(long devprops_p) {
        _devprops_p = devprops_p;
    }

    protected void finalize() {
        mdb_device_done(_devprops_p);
    }
    private native void mdb_device_done(long ptr);

    @Override
    public Mapper.Db.Device next()
    {
        if (_devprops_p != 0) {
            long temp = mdb_deref(_devprops_p);
            _devprops_p = mdb_device_next(_devprops_p);
            return new Mapper.Db.Device(temp);
        }
        else
            return null;
    }
    private native long mdb_deref(long ptr);
    private native long mdb_device_next(long ptr);

    @Override
    public boolean hasNext() { return _devprops_p != 0; }

    @Override
    public void remove() {}

    private long _devprops_p;
}
