
package Mapper.Db;

import java.util.Iterator;

public class LinkIterator implements Iterator<Mapper.Db.Link> {
    public LinkIterator(long linkprops_p) {
        _linkprops_p = linkprops_p;
    }

    protected void finalize() {
        mdb_link_done(_linkprops_p);
    }
    private native void mdb_link_done(long ptr);

    @Override
    public Mapper.Db.Link next()
    {
        if (_linkprops_p != 0) {
            long temp = mdb_deref(_linkprops_p);
            _linkprops_p = mdb_link_next(_linkprops_p);
            return new Mapper.Db.Link(temp);
        }
        else
            return null;
    }
    private native long mdb_deref(long ptr);
    private native long mdb_link_next(long ptr);

    @Override
    public boolean hasNext() { return _linkprops_p != 0; }

    @Override
    public void remove() {}

    private long _linkprops_p;
}
