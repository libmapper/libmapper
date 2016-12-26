
package mapper.map;

import java.util.Iterator;

public class Query implements Iterable<mapper.Map> {
    public Query(long map) {
        _map = map;
    }

    @Override
    public Iterator<mapper.Map> iterator() {
        Iterator<mapper.Map> it = new Iterator() {
            private long _mapcopy = mapperMapQueryCopy(_map);

            protected void finalize() {
                if (_mapcopy != 0)
                    mapperMapQueryDone(_mapcopy);
            }

            @Override
            public boolean hasNext() { return _mapcopy != 0; }

            @Override
            public mapper.Map next() {
                if (_mapcopy != 0) {
                    long temp = mapperMapDeref(_mapcopy);
                    _mapcopy = mapperMapQueryNext(_mapcopy);
                    return new mapper.Map(temp);
                }
                else
                    return null;
            }

            @Override
            public void remove() {
                throw new UnsupportedOperationException();
            }
        };
        return it;
    }

    public Query join(Query q) {
        _map = mapperMapQueryJoin(_map, q._map);
        return this;
    }
    public Query intersect(Query q) {
        _map = mapperMapQueryIntersect(_map, q._map);
        return this;
    }
    public Query subtract(Query q) {
        _map = mapperMapQuerySubtract(_map, q._map);
        return this;
    }

    protected void finalize() {
        if (_map != 0)
            mapperMapQueryDone(_map);
    }

    private native long mapperMapDeref(long ptr);
    private native long mapperMapQueryCopy(long ptr);
    private native long mapperMapQueryJoin(long lhs, long rhs);
    private native long mapperMapQueryIntersect(long lhs, long rhs);
    private native long mapperMapQuerySubtract(long lhs, long rhs);
    private native long mapperMapQueryNext(long ptr);
    private native void mapperMapQueryDone(long ptr);

    private long _map;
}
