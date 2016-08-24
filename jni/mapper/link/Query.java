
package mapper.link;

import java.util.Iterator;

public class Query implements Iterable<mapper.Link> {
    public Query(long link) {
        _link = link;
    }

    @Override
    public Iterator<mapper.Link> iterator() {
        Iterator<mapper.Link> it = new Iterator() {
            private long _linkcopy = mapperLinkQueryCopy(_link);

            protected void finalize() {
                if (_linkcopy != 0)
                    mapperLinkQueryDone(_linkcopy);
            }

            @Override
            public boolean hasNext() { return _linkcopy != 0; }

            @Override
            public mapper.Link next() {
                if (_linkcopy != 0) {
                    long temp = mapperLinkDeref(_linkcopy);
                    _linkcopy = mapperLinkQueryNext(_linkcopy);
                    return new mapper.Link(temp);
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
        _link = mapperLinkQueryJoin(_link, q._link);
        return this;
    }
    public Query intersect(Query q) {
        _link = mapperLinkQueryIntersect(_link, q._link);
        return this;
    }
    public Query subtract(Query q) {
        _link = mapperLinkQuerySubtract(_link, q._link);
        return this;
    }

    protected void finalize() {
        if (_link != 0)
            mapperLinkQueryDone(_link);
    }

    private native long mapperLinkDeref(long ptr);
    private native long mapperLinkQueryCopy(long ptr);
    private native long mapperLinkQueryJoin(long lhs, long rhs);
    private native long mapperLinkQueryIntersect(long lhs, long rhs);
    private native long mapperLinkQuerySubtract(long lhs, long rhs);
    private native long mapperLinkQueryNext(long ptr);
    private native void mapperLinkQueryDone(long ptr);

    private long _link;
}
