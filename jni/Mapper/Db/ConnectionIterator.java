
package Mapper.Db;

import java.util.Iterator;

public class ConnectionIterator implements Iterator<Mapper.Db.Connection> {
    public ConnectionIterator(long conprops_p) {
        _conprops_p = conprops_p;
    }

    protected void finalize() {
        mdb_connection_done(_conprops_p);
    }
    private native void mdb_connection_done(long ptr);

    @Override
    public Mapper.Db.Connection next()
    {
        if (_conprops_p != 0) {
            long temp = mdb_deref(_conprops_p);
            _conprops_p = mdb_connection_next(_conprops_p);
            return new Mapper.Db.Connection(temp);
        }
        else
            return null;
    }
    private native long mdb_deref(long ptr);
    private native long mdb_connection_next(long ptr);

    @Override
    public boolean hasNext() { return _conprops_p != 0; }

    @Override
    public void remove() {}

    private long _conprops_p;
}
