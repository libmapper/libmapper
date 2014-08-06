
package Mapper.Db;

import java.util.Iterator;

public class SignalIterator implements Iterator<Mapper.Db.Signal> {
    public SignalIterator(long sigprops_p) {
        _sigprops_p = sigprops_p;
    }

    protected void finalize() {
        mdb_signal_done(_sigprops_p);
    }
    private native void mdb_signal_done(long ptr);

    @Override
    public Mapper.Db.Signal next()
    {
        if (_sigprops_p != 0) {
            long temp = mdb_deref(_sigprops_p);
            _sigprops_p = mdb_signal_next(_sigprops_p);
            return new Mapper.Db.Signal(temp);
        }
        else
            return null;
    }
    private native long mdb_deref(long ptr);
    private native long mdb_signal_next(long ptr);

    @Override
    public boolean hasNext() { return _sigprops_p != 0; }

    @Override
    public void remove() {}

    private long _sigprops_p;
}
