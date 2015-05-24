
package Mapper.Db;

import java.util.Iterator;

public class MapIterator implements Iterator<Mapper.Db.Map> {
    public MapIterator(long mapprops_p) {
        _mapprops_p = mapprops_p;
    }

    protected void finalize() {
        mdb_map_done(_mapprops_p);
    }
    private native void mdb_map_done(long ptr);

    @Override
    public Mapper.Db.Map next()
    {
        if (_mapprops_p != 0) {
            long temp = mdb_deref(_mapprops_p);
            _mapprops_p = mdb_map_next(_mapprops_p);
            return new Mapper.Db.Map(temp);
        }
        else
            return null;
    }
    private native long mdb_deref(long ptr);
    private native long mdb_map_next(long ptr);

    @Override
    public boolean hasNext() { return _mapprops_p != 0; }

    @Override
    public void remove() {}

    private long _mapprops_p;
}
