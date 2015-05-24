
package Mapper.Db;

import java.util.Iterator;

public class MapCollection implements Iterable<Mapper.Db.Map>
{
    public MapCollection(long mapprops_p) {
        _mapprops_p = mapprops_p;
    }

    @Override
    public Iterator<Mapper.Db.Map> iterator() {
        return new MapIterator(_mapprops_p);
    }
    private long _mapprops_p;
}
